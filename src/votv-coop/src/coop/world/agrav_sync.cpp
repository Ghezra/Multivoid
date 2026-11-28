// coop/world/agrav_sync.cpp -- see coop/world/agrav_sync.h.

#include "coop/world/agrav_sync.h"

#include "coop/config/config.h"
#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/props/prop_rest_sync.h"
#include "coop/props/remote_prop.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/sdk_profile_names.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/engine/engine_physics.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace coop::agrav_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;

// A body whose gravity went off is told lifted once it has risen this far on the host, at the speed it rose; one that
// has not within kRiseWaitMs (blocked, or asleep) is not told.
constexpr float kRiseCm = 3.f;
constexpr uint64_t kRiseWaitMs = 10000;
// A lifted copy rising slower than this has stopped, and is set rising again.
constexpr float kStoppedCmPerSec = 2.f;
// How often the host looks while a trigger holds bodies, and how long it keeps looking once nothing it told is off.
constexpr uint64_t kScanMs = 200;
constexpr uint64_t kQuietMs = 10000;

N::Session* g_session = nullptr;
uint64_t g_nextScan = 0;
uint64_t g_quietSince = 0;
// The `comps` a trigger last held (its data pointer and count): a new gather starts the scan again.
const void* g_lastBuf = nullptr;
int32_t g_lastNum = 0;

// HOST: a body whose gravity went off, from where and when, until it is seen rising (then told) or the wait ends.
struct Pending {
    float z0;
    uint64_t t0;
};
std::unordered_map<uint32_t, Pending> g_pending;
// HOST: the bodies told lifted, by element id.
std::unordered_map<uint32_t, bool> g_lifted;
uint64_t g_rowsLifted = 0, g_rowsDropped = 0;

// CLIENT: the copies the host's float holds up, each with the host's rise in cm/s. A drive row's end (a host coast of
// a body the float knocked) leaves its copy asleep where the host's was, so once a second each one that has stopped,
// and no row drives, is set rising again.
std::unordered_map<uint32_t, float> g_clientLifted;
uint64_t g_nextRelift = 0;
uint64_t g_applied = 0, g_missing = 0, g_relifts = 0;

uint64_t Now() { return ::GetTickCount64(); }

// VOTVCOOP_AGRAV_TRACE=1: every lift and drop, by element id, with the body's height then, on either role (`[AGRAV]`).
bool Trace() {
    static const bool t = coop::config::ReadEnv("VOTVCOOP_AGRAV_TRACE") == "1";
    return t;
}

void TraceOne(const char* what, uint32_t eid, void* actor, float vz) {
    if (!Trace() || !actor) return;
    ue_wrap::FVector at{}, lin{}, ang{};
    E::GetActorRootPhysicsVelocity(actor, lin, ang);
    if (E::TryGetActorLocation(actor, at))
        UE_LOGI("[AGRAV] %s eid=%u z=%.1f vz=%.1f spin=(%.1f,%.1f,%.1f)", what, eid, at.Z, vz, ang.X, ang.Y, ang.Z);
}

uint32_t EidOf(void* actor) {
    if (!actor) return 0;
    const coop::element::ElementId e = coop::element::Registry::Get().EidForActor(actor);
    return e == coop::element::kInvalidId ? 0u : static_cast<uint32_t>(e);
}

void* ActorOf(uint32_t eid) {
    return eid ? coop::element::LivePropActor(static_cast<coop::element::ElementId>(eid)) : nullptr;
}

float ZOf(void* actor) {
    ue_wrap::FVector at{};
    return actor && E::TryGetActorLocation(actor, at) ? at.Z : 0.f;
}

// A live trigger's `comps`: TArray<UPrimitiveComponent*>, read in place. Its gather fills `comps`; the float copies it to
// `buf` and takes each body off `buf` as it switches that body's gravity, so `buf` is empty a moment after each body
// lifts or drops, and a scan of it saw at most the one body of that moment (Ghezra/Multivoid issue 59). `comps` holds
// the gather until the next.
struct Buf {
    void* const* data = nullptr;
    int32_t num = 0;
};

Buf BufOf(void* trigger) {
    static int32_t s_off = -2;
    if (s_off == -2) {
        s_off = R::FindPropertyOffset(R::ClassOf(trigger), L"comps");
        if (s_off < 0) UE_LOGW("agrav_sync: trigger_agrav_C has no `comps` -- the float is not followed");
    }
    Buf b;
    if (s_off < 0) return b;
    const auto* arr = reinterpret_cast<const uint8_t*>(trigger) + s_off;
    b.data = *reinterpret_cast<void* const* const*>(arr);
    b.num = *reinterpret_cast<const int32_t*>(arr + sizeof(void*));
    if (!b.data || b.num < 0 || b.num > 4096) b = Buf{};
    return b;
}

std::vector<void*> Triggers() {
    std::vector<void*> out;
    if (void* const cls = OI::ClassByName(L"trigger_agrav_C"))
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

void Send(bool lifted, const std::vector<N::AgravRow>& rows) {
    if (!g_session || rows.empty()) return;
    for (size_t at = 0; at < rows.size(); at += N::kAgravRowsMax) {
        N::AgravStatePayload p{};
        p.lifted = lifted ? 1 : 0;
        const size_t n = (std::min)(rows.size() - at, static_cast<size_t>(N::kAgravRowsMax));
        p.count = static_cast<uint16_t>(n);
        for (size_t i = 0; i < n; ++i) p.row[i] = rows[at + i];
        if (!g_session->SendReliable(N::ReliableKind::AgravState, &p, sizeof(p)))
            UE_LOGW("agrav_sync: an AgravState of %zu prop(s) was not sent", n);
    }
    (lifted ? g_rowsLifted : g_rowsDropped) += rows.size();
    UE_LOGI("agrav_sync: told %zu prop(s) %s (%llu lifted, %llu dropped in all)", rows.size(),
            lifted ? "lifted" : "dropped", static_cast<unsigned long long>(g_rowsLifted),
            static_cast<unsigned long long>(g_rowsDropped));
}

// A row for `eid`: its rise, and for a lift the spin the float gave it (`actor`'s own, read when it is told).
N::AgravRow Row(uint32_t eid, float vz, void* actor = nullptr) {
    N::AgravRow r{};
    r.eid = eid;
    r.vz = static_cast<int16_t>(std::clamp(vz * 10.f, -32000.f, 32000.f));
    ue_wrap::FVector lin{}, ang{};
    if (actor && E::GetActorRootPhysicsVelocity(actor, lin, ang)) {
        const float w[3] = {ang.X, ang.Y, ang.Z};
        for (int i = 0; i < 3; ++i) r.w[i] = static_cast<int16_t>(std::clamp(w[i] * 10.f, -32000.f, 32000.f));
    }
    return r;
}

void HostScan() {
    const uint64_t now = Now();
    bool fresh = false;
    for (void* t : Triggers()) {
        const Buf b = BufOf(t);
        if (b.data != g_lastBuf || b.num != g_lastNum) {
            g_lastBuf = b.data;
            g_lastNum = b.num;
            fresh = true;
        }
        for (int32_t i = 0; i < b.num; ++i) {
            void* const comp = b.data[i];
            if (!comp || !R::IsLive(comp)) continue;
            void* const actor = R::OuterOf(comp);
            const uint32_t eid = EidOf(actor);
            if (!eid || g_lifted.count(eid) || g_pending.count(eid)) continue;
            // A body that does not simulate here (a frozen prop) is not moved by the float.
            if (!E::IsComponentGravityEnabled(comp) && E::IsComponentSimulatingPhysics(comp))
                g_pending[eid] = Pending{ZOf(actor), now};
        }
    }
    std::vector<N::AgravRow> lift, drop;
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        void* const actor = ActorOf(it->first);
        void* const root = actor ? E::RootComponentOf(actor) : nullptr;
        const float rose = actor ? ZOf(actor) - it->second.z0 : 0.f;
        const uint64_t waited = now - it->second.t0;
        if (!root || E::IsComponentGravityEnabled(root) || waited > kRiseWaitMs) {
            it = g_pending.erase(it);   // back on, gone, or never rose: nothing to tell
        } else if (rose > kRiseCm && waited >= kScanMs) {
            const float vz = rose * 1000.f / static_cast<float>(waited);
            lift.push_back(Row(it->first, vz, actor));
            TraceOne("lift", it->first, actor, vz);
            g_lifted[it->first] = true;
            it = g_pending.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_lifted.begin(); it != g_lifted.end();) {
        void* const actor = ActorOf(it->first);
        void* const root = actor ? E::RootComponentOf(actor) : nullptr;
        if (!root || E::IsComponentGravityEnabled(root)) {
            if (root) {
                drop.push_back(Row(it->first, 0.f));
                TraceOne("drop", it->first, actor, 0.f);
            }
            it = g_lifted.erase(it);
        } else {
            ++it;
        }
    }
    Send(true, lift);
    Send(false, drop);
    if (fresh || !lift.empty() || !drop.empty() || !g_lifted.empty() || !g_pending.empty() || !g_quietSince)
        g_quietSince = now;
}

void Relift() {
    const uint64_t now = Now();
    if (g_clientLifted.empty() || now < g_nextRelift) return;
    g_nextRelift = now + 1000;
    for (const auto& [eid, vz] : g_clientLifted) {
        void* const actor = ActorOf(eid);
        void* const root = actor ? E::RootComponentOf(actor) : nullptr;
        if (!root || coop::remote_prop::DrivingSlotOf(actor) >= 0) continue;
        if (E::IsComponentGravityEnabled(root)) E::SetComponentGravity(root, false);
        // Only a copy that has stopped (a coast's end left it asleep): one still rising keeps its own speed.
        ue_wrap::FVector lin{}, ang{};
        if (E::GetActorRootPhysicsVelocity(actor, lin, ang) && lin.Z > kStoppedCmPerSec) continue;
        E::SetComponentLinearVelocity(root, 0.f, 0.f, vz);
        ++g_relifts;
    }
}

// ---- The float's show (EventCue 135, 136) ----

namespace sg = ue_wrap::script_gate;
UE_FACT_FN(kRowCloak, "agrav_sync", L"trigger_agrav_C", L"cloak", Declared);
constexpr int kCloakTag = 0x41475601;  // 'AGV'
bool g_cloakWatched = false, g_cloakSettled = false;
bool g_floating = false;                 // HOST: the show is on
float g_floatAt[3] = {};
void* g_localTrigger = nullptr;          // CLIENT: its own copy of the trigger, which only shows
uint32_t g_cuesSent = 0, g_cuesShown = 0;

void* ObjField(void* o, const wchar_t* name) {
    const int32_t off = o ? R::FindPropertyOffset(R::ClassOf(o), name) : -1;
    void* const v = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(o) + off) : nullptr;
    return v && R::IsLive(v) ? v : nullptr;
}

bool CallBool2(void* o, const wchar_t* fn, const wchar_t* a, bool va, const wchar_t* b, bool vb) {
    ue_wrap::ParamFrame f(o ? R::FindDispatchFunctionCached(R::ClassOf(o), fn) : nullptr);
    return f.valid() && f.Set<bool>(a, va) && (!b || f.Set<bool>(b, vb)) && ue_wrap::Call(o, f);
}

void SendFloatCue(uint32_t cue, int slot) {
    N::EventCuePayload p{};
    p.cueId = cue;
    p.x = g_floatAt[0];
    p.y = g_floatAt[1];
    p.z = g_floatAt[2];
    const bool ok = slot >= 0 ? g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::EventCue, &p, sizeof(p))
                              : g_session->SendReliable(N::ReliableKind::EventCue, &p, sizeof(p));
    if (ok) ++g_cuesSent;
}

// HOST: the trigger's cloak is its show's begin (forward) and end (back).
void OnCloakPost(const sg::Call& call) {
    if (!g_session || g_session->role() != N::Role::Host || !g_session->connected() || !call.object || !call.locals)
        return;
    int32_t off = -1;
    uint8_t mask = 0;
    if (!R::FindBoolProperty(call.function, L"forward", off, mask)) return;
    const bool forward = (call.locals[off] & mask) != 0;
    if (forward == g_floating) return;
    g_floating = forward;
    ue_wrap::FVector at{};
    if (E::TryGetActorLocation(call.object, at)) g_floatAt[0] = at.X, g_floatAt[1] = at.Y, g_floatAt[2] = at.Z;
    SendFloatCue(forward ? kCueFloatBegin : kCueFloatEnd, -1);
    UE_LOGI("agrav_sync: HOST the float's show %s at (%.0f,%.0f,%.0f) -- sent", forward ? "begins" : "ends",
            g_floatAt[0], g_floatAt[1], g_floatAt[2]);
}

// CLIENT: its own trigger at the host's, spawned on the first begin and kept for the next.
void* LocalTrigger(const float at[3]) {
    if (g_localTrigger && R::IsLive(g_localTrigger)) return g_localTrigger;
    g_localTrigger = nullptr;
    void* const cls = OI::ClassByName(L"trigger_agrav_C");
    if (cls) g_localTrigger = E::SpawnActor(cls, ue_wrap::FVector{at[0], at[1], at[2]});
    return g_localTrigger;
}

void ShowBegin(const float at[3]) {
    void* const t = LocalTrigger(at);
    if (!t) {
        UE_LOGW("agrav_sync: CLIENT no trigger of its own to show the float on");
        return;
    }
    CallBool2(t, L"cloak", L"forward", true, nullptr, false);
    void* const light = ObjField(t, L"pointlight");
    void* const cam = ue_wrap::world_singleton::Find(ue_wrap::profile::name::PlayerCameraManagerClass);
    void* const camRoot = cam ? E::RootComponentOf(cam) : nullptr;
    if (light && camRoot) {
        ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(light), L"K2_AttachToComponent"));
        const R::FName none{};
        if (f.valid() && f.Set<void*>(L"Parent", camRoot) && f.SetRaw(L"SocketName", &none, sizeof(none))) {
            f.Set<uint8_t>(L"LocationRule", 0);
            f.Set<uint8_t>(L"RotationRule", 0);
            f.Set<uint8_t>(L"ScaleRule", 0);
            f.Set<bool>(L"bWeldSimulatedBodies", false);
            ue_wrap::Call(light, f);
        }
        CallBool2(light, L"SetVisibility", L"bNewVisibility", true, L"bPropagateToChildren", false);
    }
    const bool hum = CallBool2(ObjField(t, L"arirhover_cue"), L"SetActive", L"bNewActive", true, L"bReset", false);
    ++g_cuesShown;
    UE_LOGI("agrav_sync: CLIENT the float's show begins (light=%d hum=%d)", light && camRoot ? 1 : 0, hum ? 1 : 0);
}

void ShowEnd() {
    void* const t = g_localTrigger && R::IsLive(g_localTrigger) ? g_localTrigger : nullptr;
    if (!t) return;
    CallBool2(ObjField(t, L"arirhover_cue"), L"SetActive", L"bNewActive", false, L"bReset", false);
    CallBool2(ObjField(t, L"pointlight"), L"SetVisibility", L"bNewVisibility", false, L"bPropagateToChildren", false);
    CallBool2(t, L"cloak", L"forward", false, nullptr, false);
    ++g_cuesShown;
    UE_LOGI("agrav_sync: CLIENT the float's show ends");
}

void TickCloakWatch() {
    if (!g_cloakWatched) {
        if (!sg::WatchName(kRowCloak, kCloakTag, nullptr, &OnCloakPost)) return;
        g_cloakWatched = true;
    }
    if (g_cloakSettled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowCloak, kCloakTag)) return;
    g_cloakSettled = true;
    if (sg::NameWatchLive(kRowCloak, kCloakTag)) UE_LOGI("agrav_sync: the watch on the float's cloak is live");
    else UE_LOGE("agrav_sync: the cloak watch is not live -- a client sees no float's show");
}

}  // namespace

void Install(N::Session* session) { g_session = session; }

bool OnCue(const N::EventCuePayload& p) {
    if (p.cueId != kCueFloatBegin && p.cueId != kCueFloatEnd) return false;
    if (!g_session || g_session->role() != N::Role::Client) return true;
    if (p.cueId == kCueFloatBegin) {
        const float at[3] = {p.x, p.y, p.z};
        if (std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2])) ShowBegin(at);
    } else {
        ShowEnd();
    }
    return true;
}

void SendJoinSnapshotForSlot(int slot) {
    if (!g_session || g_session->role() != N::Role::Host || slot <= 0 || !g_floating) return;
    SendFloatCue(kCueFloatBegin, slot);
}

void Tick() {
    if (!g_session) return;
    TickCloakWatch();
    if (g_session->role() == N::Role::Client) {
        Relift();
        return;
    }
    if (g_session->role() != N::Role::Host || !g_session->connected()) return;
    const uint64_t now = Now();
    if (now < g_nextScan) return;
    // Quiet (nothing told or waiting, and the buffers unchanged for a while): look once a second for a new gather.
    const bool quiet = g_lifted.empty() && g_pending.empty() && g_quietSince && now - g_quietSince > kQuietMs;
    g_nextScan = now + (quiet ? 1000 : kScanMs);
    if (quiet) {
        for (void* t : Triggers()) {
            const Buf b = BufOf(t);
            if (b.data != g_lastBuf || b.num != g_lastNum) { HostScan(); return; }
        }
        return;
    }
    HostScan();
}

void OnState(const N::AgravStatePayload& p) {
    const bool lifted = p.lifted != 0;
    int done = 0, missing = 0;
    for (int i = 0; i < p.count && i < N::kAgravRowsMax; ++i) {
        const uint32_t eid = p.row[i].eid;
        const float vz = static_cast<float>(p.row[i].vz) / 10.f;
        void* const actor = ActorOf(eid);
        void* const root = actor ? E::RootComponentOf(actor) : nullptr;
        if (!root || !E::IsPrimitiveComponent(root)) { ++missing; continue; }
        E::SetComponentGravity(root, !lifted);
        if (lifted) {
            g_clientLifted[eid] = vz;
            E::SetComponentLinearVelocity(root, 0.f, 0.f, vz);
            // The host's spin, so the copy turns as the host's does (before, each copy kept its own).
            E::SetComponentAngularVelocity(root, p.row[i].w[0] / 10.f, p.row[i].w[1] / 10.f, p.row[i].w[2] / 10.f);
            TraceOne("lift", eid, actor, vz);
        } else {
            TraceOne("drop", eid, actor, vz);
            g_clientLifted.erase(eid);
            coop::prop_rest_sync::NoteLocalMove(actor);
        }
        ++done;
    }
    g_applied += done;
    g_missing += missing;
    UE_LOGI("agrav_sync: the host %s %d prop(s) here (%d with no copy here)", lifted ? "lifted" : "dropped", done,
            missing);
}

void OnDisconnect() {
    if (g_cuesSent || g_cuesShown)
        UE_LOGI("agrav_sync: session end -- float show cues sent %u, shown %u", g_cuesSent, g_cuesShown);
    g_cuesSent = g_cuesShown = 0;
    g_floating = false;
    if (g_localTrigger && R::IsLive(g_localTrigger)) E::DestroyActor(g_localTrigger);
    g_localTrigger = nullptr;
    g_pending.clear();
    g_lifted.clear();
    g_clientLifted.clear();
    g_lastBuf = nullptr;
    g_lastNum = 0;
    g_quietSince = 0;
    g_nextScan = 0;
    if (g_rowsLifted || g_applied)
        UE_LOGI("agrav_sync: session over -- told %llu lifted and %llu dropped; applied %llu, %llu with no copy, %llu "
                "lifts set again", static_cast<unsigned long long>(g_rowsLifted),
                static_cast<unsigned long long>(g_rowsDropped), static_cast<unsigned long long>(g_applied),
                static_cast<unsigned long long>(g_missing), static_cast<unsigned long long>(g_relifts));
    g_rowsLifted = g_rowsDropped = g_applied = g_missing = g_relifts = 0;
}

}  // namespace coop::agrav_sync
