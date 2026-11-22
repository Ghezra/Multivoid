// coop/dev/event_probe.cpp -- [dev] what an event leaves in the world, on each peer (VOTVCOOP_EVENT_PROBE=<event>, with
// VOTVCOOP_EVENT_PROBE_CLASS=<substring>, on both peers), for Ghezra/Multivoid#39.
//
// THE HOST, 24 s after a client is connected: arms the event and drives its trigger box's own overlap with its player,
// as a walk-in does (coop/dev/event_force), or, for an event with no box, fires it as the dev event menu does; a
// "Class.function" instead runs that paramless function of the class's last live actor. BOTH
// PEERS, at 20 s, 34 s, 50 s and 150 s: every live actor whose class name holds the substring (any case), with its place.

#include "coop/dev/event_probe.h"

#include "coop/config/config.h"
#include "coop/dev/event_force.h"
#include "coop/dev/event_trigger.h"
#include "coop/net/session.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/world/daynightcycle.h"
#include "ue_wrap/world/email.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace coop::dev::event_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace OI = ue_wrap::object_index;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Event() {
    static const std::string v = coop::config::ReadEnv("VOTVCOOP_EVENT_PROBE");
    return v;
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

uint64_t g_startAt = 0;
int g_census = 0;
bool g_fired = false;
bool g_unpassed = false;

// VOTVCOOP_EVENT_PROBE_UNPASS=1, on every peer at 10 s: the event's row taken out of this peer's passed events, so a
// past row the save carries is replayed as its first fire is, not skipped as history.
void Unpass() {
    void* const ss = ue_wrap::daynightcycle::SaveSlotOfCycle(ue_wrap::daynightcycle::Cycle());
    const int32_t off = ss ? R::FindPropertyOffset(R::ClassOf(ss), L"passEvents") : -1;
    if (off < 0) return;
    struct Arr { R::FName* data; int32_t num, max; };
    auto* a = reinterpret_cast<Arr*>(static_cast<uint8_t*>(ss) + off);
    const std::wstring want(Event().begin(), Event().end());
    int removed = 0;
    for (int32_t i = 0; a->data && i < a->num;) {
        if (R::ToString(a->data[i]) == want) {
            a->data[i] = a->data[a->num - 1];
            --a->num;
            ++removed;
        } else {
            ++i;
        }
    }
    UE_LOGI("[EVPROBE] '%s' taken out of this peer's passed events (%d)", Event().c_str(), removed);
}

void Census(const char* when) {
    const std::string narrow = coop::config::ReadEnv("VOTVCOOP_EVENT_PROBE_CLASS");
    const std::wstring sub = Lower(std::wstring(narrow.begin(), narrow.end()));
    if (sub.empty()) return;
    std::vector<void*> classes;
    OI::ForEachClass([](void* ctx, void* cls, void*) { static_cast<std::vector<void*>*>(ctx)->push_back(cls); }, &classes);
    int n = 0;
    for (void* cls : classes) {
        const std::wstring name = R::ToString(R::NameOf(cls));
        // Several substrings, '|' between them: a class matches any one.
        bool match = false;
        for (size_t at = 0; at <= sub.size();) {
            const size_t bar = sub.find(L'|', at);
            const std::wstring one = sub.substr(at, bar == std::wstring::npos ? std::wstring::npos : bar - at);
            if (!one.empty() && Lower(name).find(one) != std::wstring::npos) match = true;
            if (bar == std::wstring::npos) break;
            at = bar + 1;
        }
        if (!match) continue;
        struct Ctx { const std::wstring* name; int* n; const char* when; } ctx{&name, &n, when};
        OI::ForEachInstance(cls, [](void* c, void* obj, int32_t) {
            auto* x = static_cast<Ctx*>(c);
            if (!obj || !R::IsLive(obj) || R::NameStartsWith(R::NameOf(obj), L"Default__")) return;
            ue_wrap::FVector at{};
            if (!ue_wrap::engine::TryGetActorLocation(obj, at)) return;   // actors only
            ++*x->n;
            // An `active` bit, where the class has one (a camera's), and the actor's owner (a child actor's parent).
            int32_t off = -1;
            uint8_t mask = 0;
            const int active = R::FindBoolProperty(R::ClassOf(obj), L"active", off, mask) && off >= 0
                                   ? ((static_cast<const uint8_t*>(obj)[off] & mask) != 0 ? 1 : 0) : -1;
            void* parent = nullptr;
            ue_wrap::engine::GetAttachParentActor(obj, parent);
            // VOTVCOOP_EVENT_PROBE_FIELD=<name>: that field's first four bytes as an integer, where the class has it.
            static const std::string s_field = coop::config::ReadEnv("VOTVCOOP_EVENT_PROBE_FIELD");
            const std::wstring wfield(s_field.begin(), s_field.end());
            const int32_t foff = wfield.empty() ? -1 : R::FindPropertyOffset(R::ClassOf(obj), wfield.c_str());
            int32_t fval = 0;
            if (foff >= 0) std::memcpy(&fval, static_cast<const uint8_t*>(obj) + foff, sizeof(fval));
            UE_LOGI("[EVPROBE] %s: %ls '%ls' at (%.0f,%.0f,%.0f) active=%d parent=%ls %s=%d", x->when, x->name->c_str(),
                    R::ToString(R::NameOf(obj)).c_str(), at.X, at.Y, at.Z, active,
                    parent ? R::ClassNameOf(parent).c_str() : L"-", foff >= 0 ? s_field.c_str() : "-", fval);
        }, &ctx);
    }
    UE_LOGI("[EVPROBE] %s: %d actor(s) of a class holding '%ls'", when, n, sub.c_str());
    // VOTVCOOP_EVENT_PROBE_EMAILS=1: this peer's inbox size, the rows its own game added included.
    if (!coop::config::ReadEnv("VOTVCOOP_EVENT_PROBE_EMAILS").empty() && ue_wrap::email::EnsureResolved())
        UE_LOGI("[EVPROBE] %s: %d email(s) in this peer's inbox", when, ue_wrap::email::EmailCount());
    // VOTVCOOP_EVENT_PROBE_CAMS=1: the gamemode's `cams`, the laptop's camera list (each camera adds itself).
    if (coop::config::ReadEnv("VOTVCOOP_EVENT_PROBE_CAMS").empty()) return;
    void* const gm = ue_wrap::world_singleton::Gamemode();
    const int32_t off = gm ? R::FindPropertyOffset(R::ClassOf(gm), L"cams") : -1;
    if (off < 0) return;
    struct Arr { void** data; int32_t num; int32_t max; };
    const auto* a = reinterpret_cast<const Arr*>(static_cast<const uint8_t*>(gm) + off);
    for (int32_t i = 0; i < a->num && i < 64; ++i) {
        void* const c = a->data[i];
        if (!c || !R::IsLive(c)) {
            UE_LOGI("[EVPROBE] %s: cams[%d] none", when, i);
            continue;
        }
        int32_t ao = -1;
        uint8_t am = 0;
        const int active = R::FindBoolProperty(R::ClassOf(c), L"active", ao, am) && ao >= 0
                               ? ((static_cast<const uint8_t*>(c)[ao] & am) != 0 ? 1 : 0) : -1;
        void* parent = nullptr;
        ue_wrap::engine::GetAttachParentActor(c, parent);
        UE_LOGI("[EVPROBE] %s: cams[%d] %ls active=%d parent=%ls", when, i, R::ClassNameOf(c).c_str(), active,
                parent ? R::ClassNameOf(parent).c_str() : L"-");
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Event().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[EVPROBE] armed role=%s event=%s", host ? "host" : "client", Event().c_str());
    }
    const uint64_t t = Now() - g_startAt;
    static const bool s_unpass = !coop::config::ReadEnv("VOTVCOOP_EVENT_PROBE_UNPASS").empty();
    if (s_unpass && !g_unpassed && t >= 10000u) {
        g_unpassed = true;
        Unpass();
    }
    static const uint64_t kCensusAt[] = {20000u, 34000u, 50000u, 150000u};
    static const char* const kWhen[] = {"before", "after", "later", "final"};
    if (g_census < 4 && t >= kCensusAt[g_census]) {
        Census(kWhen[g_census]);
        ++g_census;
    }
    if (host && !g_fired && t >= 24000u) {
        g_fired = true;
        // "Class.function" instead of an event: the host runs that function of the class's last live actor.
        const size_t dot = Event().find('.');
        if (dot != std::string::npos) {
            const std::wstring cls(Event().begin(), Event().begin() + static_cast<std::ptrdiff_t>(dot));
            const std::wstring fn(Event().begin() + static_cast<std::ptrdiff_t>(dot) + 1, Event().end());
            void* target = nullptr;
            for (void* o : R::FindObjectsByClass(cls.c_str()))
                if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) target = o;
            void* const f = target ? R::FindDispatchFunctionCached(R::ClassOf(target), fn.c_str()) : nullptr;
            ue_wrap::ParamFrame pf(f);
            const bool ran = f && pf.valid() && ue_wrap::Call(target, pf);
            UE_LOGI("[EVPROBE] host: %s ran=%d", Event().c_str(), ran ? 1 : 0);
            return;
        }
        bool ok = coop::dev::event_force::ForceNow(Event().c_str());
        const char* how = "forced through its box";
        if (!ok) {
            how = "fired";
            for (const auto& ev : coop::dev::event_trigger::Events())
                if (Event() == ev.name) ok = coop::dev::event_trigger::Trigger(ev);
        }
        UE_LOGI("[EVPROBE] host: '%s' %s (ok=%d)", Event().c_str(), how, ok ? 1 : 0);
    }
}

}  // namespace coop::dev::event_probe
