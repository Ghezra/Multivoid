// coop/world/radio_sync.cpp -- see coop/world/radio_sync.h.

#include "coop/world/radio_sync.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/props/prop_key_index.h"
#include "coop/props/remote_prop.h"  // KeyToWString

#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/world/radio.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace coop::radio_sync {
namespace {

namespace RD = ue_wrap::radio;
using Clock = std::chrono::steady_clock;

constexpr auto kPoll = std::chrono::milliseconds(250);
constexpr auto kHeartbeat = std::chrono::seconds(5);  // a late copy's time, a joiner's first state
constexpr auto kPredictFor = std::chrono::seconds(3);  // this client's own edit stands until the host has answered

std::atomic<coop::net::Session*> g_session{nullptr};
Clock::time_point g_nextPoll{};

// HOST: what went out last.
struct Sent {
    RD::State state;
    Clock::time_point at{};
};
std::unordered_map<std::wstring, Sent> g_sent;
std::unordered_map<std::wstring, RD::State> g_edit;

// CLIENT: the state this copy is moved to, and what the player's interface last read.
struct Want {
    RD::State state;
    Clock::time_point at{};
    Clock::time_point predictUntil{};
    bool have = false;
    bool warned = false;
    RD::State seen;
    bool seenAny = false;
};
std::unordered_map<std::wstring, Want> g_want;

uint32_t g_published = 0, g_edits = 0, g_applied = 0, g_missing = 0;

std::wstring KeyOf(void* actor) {
    const std::wstring key = ue_wrap::prop::GetInteractableKeyString(actor);
    return key == L"None" ? std::wstring{} : key;
}

void Collect(std::vector<void*>& out) {
    struct Walk {
        void* world;
        std::vector<void*>* out;
    } walk{ue_wrap::world_identity::CurrentWorld(), &out};
    void* const cls = ue_wrap::object_index::ClassByName(RD::kClass);
    if (!walk.world || !cls) return;
    ue_wrap::object_index::ForEachInstance(
        cls,
        [](void* ctx, void* obj, int32_t) {
            auto* w = static_cast<Walk*>(ctx);
            if (ue_wrap::world_identity::WorldOf(obj) == w->world) w->out->push_back(obj);
        },
        &walk);
}

coop::net::RadioStatePayload Encode(const std::wstring& key, const RD::State& state) {
    coop::net::RadioStatePayload p{};
    for (size_t i = 0; i < key.size() && i < sizeof(p.key.data); ++i)
        p.key.data[p.key.len++] = static_cast<char>(key[i]);  // save keys are ASCII
    p.track = state.track;
    p.ticks = state.ticks;
    p.playing = state.playing ? 1 : 0;
    p.mode = state.mode;
    return p;
}

RD::State Decode(const coop::net::RadioStatePayload& p) {
    RD::State state;
    state.track = p.track;
    state.ticks = p.ticks;
    state.playing = p.playing != 0;
    state.mode = p.mode;
    return state;
}

bool SameChoice(const RD::State& a, const RD::State& b) {
    return a.track == b.track && a.playing == b.playing && a.mode == b.mode;
}

void PollHost(coop::net::Session& s, const std::vector<void*>& radios, Clock::time_point now) {
    for (void* actor : radios) {
        const std::wstring key = KeyOf(actor);
        if (key.empty()) continue;
        // A client's edit that met a track still opening waits here, and what this copy reads until then is not the state.
        const auto edit = g_edit.find(key);
        if (edit != g_edit.end()) {
            if (RD::ApplyState(actor, edit->second) == RD::Apply::Waiting) continue;
            g_edit.erase(edit);
        }
        RD::State state;
        if (!RD::Read(actor, state)) continue;
        auto it = g_sent.find(key);
        if (it != g_sent.end() && SameChoice(it->second.state, state) && (!state.playing || now - it->second.at < kHeartbeat))
            continue;
        const coop::net::RadioStatePayload p = Encode(key, state);
        if (!s.SendReliable(coop::net::ReliableKind::RadioState, &p, sizeof(p))) continue;
        const bool changed = it == g_sent.end() || !SameChoice(it->second.state, state);
        g_sent[key] = {state, now};
        ++g_published;
        if (changed)
            UE_LOGI("radio_sync: '%ls' track=%llx playing=%d mode=%u -- RadioState sent (#%u)", key.c_str(),
                    static_cast<unsigned long long>(state.track), state.playing ? 1 : 0, state.mode, g_published);
    }
}

void PollClient(coop::net::Session& s, const std::vector<void*>& radios, Clock::time_point now) {
    void* const open = RD::RadioWithOpenInterface(coop::players::Registry::Get().Local());
    for (void* actor : radios) {
        const std::wstring key = KeyOf(actor);
        if (key.empty()) continue;
        Want& want = g_want[key];
        RD::State read;
        const bool readable = RD::Read(actor, read);
        if (actor != open) {
            want.seenAny = false;
        } else if (readable) {  // while a track opens there is nothing to read, and the last reading stands
            if (want.seenAny && !SameChoice(want.seen, read)) {
                const coop::net::RadioStatePayload p = Encode(key, read);
                if (s.SendReliableToSlot(0, coop::net::ReliableKind::RadioState, &p, sizeof(p))) ++g_edits;
                want.state = read;
                want.at = now;
                want.have = true;
                want.predictUntil = now + kPredictFor;
                UE_LOGI("radio_sync: '%ls' changed here, track=%llx playing=%d mode=%u -- RadioState sent (#%u)", key.c_str(),
                        static_cast<unsigned long long>(read.track), read.playing ? 1 : 0, read.mode, g_edits);
            }
            want.seen = read;
            want.seenAny = true;
        }
        if (!want.have || now < want.predictUntil) continue;
        RD::State target = want.state;
        if (target.playing)
            target.ticks += std::chrono::duration_cast<std::chrono::microseconds>(now - want.at).count() * 10;
        const RD::Apply result = RD::ApplyState(actor, target);
        if (result == RD::Apply::NoSuchTrack && !want.warned) {
            want.warned = true;
            ++g_missing;
            UE_LOGI("radio_sync: '%ls' plays a track this peer cannot open (not in its catalog, or Custom Content off) -- "
                    "silent here", key.c_str());
        }
        if (result == RD::Apply::Done) want.warned = false;
    }
}

}  // namespace

void Install(coop::net::Session* session) { g_session.store(session, std::memory_order_release); }

void Tick() {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->running() || !s->connected()) return;
    const auto now = Clock::now();
    if (now < g_nextPoll) return;
    g_nextPoll = now + kPoll;
    std::vector<void*> radios;
    Collect(radios);
    if (s->role() == coop::net::Role::Host) PollHost(*s, radios, now);
    else PollClient(*s, radios, now);
}

void OnState(int senderSlot, const coop::net::RadioStatePayload& p) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s) return;
    const std::wstring key = coop::remote_prop::KeyToWString(p.key);
    if (s->role() == coop::net::Role::Client) {
        Want& want = g_want[key];
        want.state = Decode(p);
        want.at = Clock::now();
        want.have = true;
        ++g_applied;
        return;
    }
    void* const actor = coop::prop_key_index::Find(key);
    if (!actor || !RD::IsRadio(actor)) {
        UE_LOGI("radio_sync: slot %d's edit of '%ls' names no radio here -- dropped", senderSlot, key.c_str());
        return;
    }
    const RD::Apply result = RD::ApplyState(actor, Decode(p));
    if (result == RD::Apply::Waiting) g_edit[key] = Decode(p);
    else g_edit.erase(key);
    ++g_applied;
    UE_LOGI("radio_sync: slot %d set '%ls' track=%llx playing=%u mode=%u (%hs)", senderSlot, key.c_str(),
            static_cast<unsigned long long>(p.track), p.playing, p.mode,
            result == RD::Apply::NoSuchTrack ? "the host cannot open it"
                                             : (result == RD::Apply::Waiting ? "opening" : "applied"));
}

void QueueConnectBroadcastForSlot(int /*slot*/) { g_sent.clear(); }

void OnDisconnect() {
    if (g_published || g_edits || g_applied || g_missing)
        UE_LOGI("radio_sync: session end -- published=%u edits=%u applied=%u missing=%u", g_published, g_edits, g_applied,
                g_missing);
    g_sent.clear();
    g_edit.clear();
    g_want.clear();
    g_nextPoll = {};
    g_published = g_edits = g_applied = g_missing = 0;
}

}  // namespace coop::radio_sync
