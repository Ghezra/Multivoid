// coop/props/tape_sync.cpp -- see coop/props/tape_sync.h.
//
// 0.9.0n. prop_officetape_C: playerHoldPre keeps the holder in `player` and turns the tick on; ReceiveTick reads the
// holder's hitresult and keeps the prop it names in `prop` (and the prop health widget on it). playerHandUse_LMB with a
// `prop`: its physicsImpact's `health` equal to damageData's health -> the hint "The object is fine"; else health set to
// it, `uses` - 1, the audio played, and at none or fewer K2_DestroyActor, else usesMessage. Without a `prop`, "No prop".

#include "coop/props/tape_sync.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/facts/function_row.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace coop::tape_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace sg = ue_wrap::script_gate;
namespace EL = coop::element;

UE_FACT_FN(kRowUse, "tape_sync", L"prop_officetape_C", L"playerHandUse_LMB", Declared);
constexpr int kTagUse = 0x54415001;  // 'TAP'

constexpr const wchar_t* kTapeClass = L"prop_officetape_C";
constexpr float kReachCm = 600.f;
constexpr uint64_t kGapMs = 250;

N::Session* g_session = nullptr;
bool g_watched = false;
bool g_settled = false;
void* g_applying = nullptr;  // CLIENT: the tape whose click runs for the host's answer
uint64_t g_lastAt[N::kMaxPeers] = {};
uint32_t g_asked = 0, g_mended = 0, g_answered = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }
uint64_t NowMs() { return ::GetTickCount64(); }

bool IsTape(void* obj) { return obj && R::IsLive(obj) && R::NameEquals(R::NameOf(R::ClassOf(obj)), kTapeClass); }

uint32_t EidOf(void* obj) {
    const EL::ElementId eid = EL::Registry::Get().EidForActor(obj);
    return eid == EL::kInvalidId ? 0u : static_cast<uint32_t>(eid);
}

template <class T>
T* FieldOf(void* obj, R::InstanceOffset& o) {
    const int32_t off = obj ? o.Of(obj) : -1;
    return off >= 0 ? reinterpret_cast<T*>(static_cast<char*>(obj) + off) : nullptr;
}
R::InstanceOffset s_prop(L"prop"), s_uses(L"uses"), s_impact(L"physicsImpact");

// A prop's health and its full, on its physics impact component.
struct Health {
    float* now = nullptr;
    float full = 0.f;
};
Health HealthOf(void* prop) {
    Health h;
    void* const* const compp = FieldOf<void*>(prop, s_impact);
    void* const comp = compp ? *compp : nullptr;
    if (!comp || !R::IsLive(comp)) return h;
    static void* s_cls = nullptr;
    static int32_t s_health = -1, s_data = -1, s_full = -1;
    void* const cls = R::ClassOf(comp);
    if (cls != s_cls) {
        s_cls = cls;
        s_health = R::FindPropertyOffset(cls, L"health");
        s_data = R::FindPropertyOffset(cls, L"damageData");
        void* const st = R::PropertyInnerStruct(cls, L"damageData");
        s_full = st ? R::FindPropertyOffsetByPrefix(st, L"health_") : -1;
        if (s_health < 0 || s_data < 0 || s_full < 0)
            UE_LOGE("tape_sync: the physics impact's health (%d) or damageData.health (%d+%d) did not resolve", s_health,
                    s_data, s_full);
    }
    if (s_health < 0 || s_data < 0 || s_full < 0) return h;
    h.now = reinterpret_cast<float*>(static_cast<char*>(comp) + s_health);
    std::memcpy(&h.full, static_cast<char*>(comp) + s_data + s_full, sizeof(float));
    return h;
}

void Answer(int slot, uint32_t tape, uint32_t prop, uint8_t result, int32_t uses) {
    N::TapeStatePayload p{};
    p.tape = tape;
    p.prop = prop;
    p.uses = uses;
    p.result = result;
    g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::TapeState, &p, sizeof(p));
}

// CLIENT: a click on a prop the host names is the host's to judge.
sg::Verdict OnUsePre(const sg::Call& c) {
    if (!IsClient() || !c.object || c.object == g_applying) return sg::Verdict::Run;
    void* const* const propp = FieldOf<void*>(c.object, s_prop);
    void* const prop = propp ? *propp : nullptr;
    N::TapeAskPayload p{};
    p.tape = EidOf(c.object);
    p.prop = prop && R::IsLive(prop) ? EidOf(prop) : 0u;
    if (!p.tape || !p.prop) return sg::Verdict::Run;  // no prop in view, or one the host does not name: the game's
    const bool ok = g_session->SendReliable(N::ReliableKind::TapeAsk, &p, sizeof(p));
    ++g_asked;
    UE_LOGI("tape_sync: CLIENT taped prop eid %u with tape eid %u -- asked of the host (ok=%d)", p.prop, p.tape, ok ? 1 : 0);
    return sg::Verdict::Cancel;
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    if (!sg::WatchName(kRowUse, kTagUse, &OnUsePre, nullptr)) UE_LOGE("tape_sync: the watch on the tape's use was refused");
}

void Tick() {
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowUse, kTagUse)) return;
    g_settled = true;
    if (sg::NameWatchLive(kRowUse, kTagUse)) UE_LOGI("tape_sync: the watch on the tape's use is live");
    else UE_LOGE("tape_sync: the watch on the tape's use is not live -- a client's repair mends its own copy alone");
}

void OnAsk(int senderSlot, const N::TapeAskPayload& p) {
    if (!IsHost() || senderSlot <= 0 || senderSlot >= static_cast<int>(N::kMaxPeers)) return;
    const uint64_t now = NowMs();
    if (now - g_lastAt[senderSlot] < kGapMs) return;  // a flood is dropped, not answered
    g_lastAt[senderSlot] = now;
    void* const tape = EL::LiveActorOfType(static_cast<EL::ElementId>(p.tape), EL::ElementType::Prop);
    void* const prop = EL::LiveActorOfType(static_cast<EL::ElementId>(p.prop), EL::ElementType::Prop);
    int32_t* const uses = FieldOf<int32_t>(tape, s_uses);
    const Health h = HealthOf(prop);
    coop::RemotePlayer* const rp = coop::players::Registry::Get().Puppet(static_cast<uint8_t>(senderSlot));
    void* const body = rp ? rp->actor() : nullptr;
    ue_wrap::FVector bodyAt{}, at{};
    const bool reach = body && prop && E::TryGetActorLocation(body, bodyAt) && E::TryGetActorLocation(prop, at) &&
                       (bodyAt.X - at.X) * (bodyAt.X - at.X) + (bodyAt.Y - at.Y) * (bodyAt.Y - at.Y) +
                               (bodyAt.Z - at.Z) * (bodyAt.Z - at.Z) <= kReachCm * kReachCm;
    if (!IsTape(tape) || !uses || *uses <= 0 || !h.now || !reach) {
        UE_LOGW("tape_sync: HOST slot %d's tape eid %u on prop eid %u refused (tape %d, prop's health %d, in reach %d)",
                senderSlot, p.tape, p.prop, IsTape(tape) ? 1 : 0, h.now ? 1 : 0, reach ? 1 : 0);
        Answer(senderSlot, p.tape, p.prop, N::kTapeRefused, uses ? *uses : 0);
        return;
    }
    if (*h.now == h.full) {
        Answer(senderSlot, p.tape, p.prop, N::kTapeFine, *uses);
        UE_LOGI("tape_sync: HOST slot %d's tape on prop eid %u: the prop is at full (%.1f)", senderSlot, p.prop, h.full);
        return;
    }
    const float was = *h.now;
    *h.now = h.full;
    --*uses;
    ++g_mended;
    Answer(senderSlot, p.tape, p.prop, N::kTapeMended, *uses);
    UE_LOGI("tape_sync: HOST slot %d mended prop eid %u with tape eid %u: health %.1f -> %.1f, %d uses left", senderSlot,
            p.prop, p.tape, was, h.full, *uses);
}

void OnState(const N::TapeStatePayload& p) {
    if (!IsClient()) return;
    ++g_answered;
    void* const tape = EL::LiveActorOfType(static_cast<EL::ElementId>(p.tape), EL::ElementType::Prop);
    void* const prop = EL::LiveActorOfType(static_cast<EL::ElementId>(p.prop), EL::ElementType::Prop);
    int32_t* const uses = FieldOf<int32_t>(tape, s_uses);
    void** const propp = FieldOf<void*>(tape, s_prop);
    const Health h = HealthOf(prop);
    if (p.result == N::kTapeRefused || !IsTape(tape) || !uses || !propp || !h.now) {
        if (uses && p.result == N::kTapeRefused) *uses = p.uses;
        UE_LOGI("tape_sync: CLIENT tape eid %u on prop eid %u: %s", p.tape, p.prop,
                p.result == N::kTapeRefused ? "refused by the host" : "not here to show");
        return;
    }
    // The click runs on this copy as the host found it: at full the game's "fine", else one repair to the host's uses.
    *propp = prop;
    if (p.result == N::kTapeMended) {
        *uses = p.uses + 1;
        *h.now = h.full > 1.f ? h.full - 1.f : 0.f;
    } else {
        *h.now = h.full;
    }
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(tape), L"playerHandUse_LMB"));
    if (f.valid()) f.Set<void*>(L"player", me);
    g_applying = tape;
    const bool ran = f.valid() && ue_wrap::Call(tape, f);
    g_applying = nullptr;
    UE_LOGI("tape_sync: CLIENT tape eid %u on prop eid %u: %s, %d uses left (ran=%d)", p.tape, p.prop,
            p.result == N::kTapeMended ? "mended by the host" : "at full", p.uses, ran ? 1 : 0);
}

void OnDisconnect() {
    if (g_asked || g_mended || g_answered)
        UE_LOGI("tape_sync: session end -- client asks %u, answers %u; host repairs %u", g_asked, g_answered, g_mended);
    g_asked = g_mended = g_answered = 0;
    g_applying = nullptr;
    for (uint64_t& t : g_lastAt) t = 0;
}

}  // namespace coop::tape_sync
