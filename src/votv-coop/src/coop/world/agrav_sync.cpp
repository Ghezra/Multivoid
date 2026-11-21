// coop/world/agrav_sync.cpp -- see coop/world/agrav_sync.h.

#include "coop/world/agrav_sync.h"

#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/props/prop_rest_sync.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_physics.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace coop::agrav_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;

// The event's own lift: setPhysicsLinearVelocity((0, 0, 10), bAddToCurrent true).
constexpr float kLiftCmPerSec = 10.f;
// How often the host looks while a trigger holds bodies, and how long it keeps looking once nothing it told is off.
constexpr uint64_t kScanMs = 200;
constexpr uint64_t kQuietMs = 10000;

N::Session* g_session = nullptr;
uint64_t g_nextScan = 0;
uint64_t g_quietSince = 0;
// The `buf` a trigger last held (its data pointer and count): a new gather starts the scan again.
const void* g_lastBuf = nullptr;
int32_t g_lastNum = 0;
// HOST: the bodies told lifted, by element id.
std::unordered_map<uint32_t, bool> g_lifted;
uint64_t g_rowsLifted = 0, g_rowsDropped = 0;
// CLIENT: the switches applied, and the ones with no copy here.
uint64_t g_applied = 0, g_missing = 0;

uint64_t Now() { return ::GetTickCount64(); }

uint32_t EidOf(void* actor) {
    if (!actor) return 0;
    const coop::element::ElementId e = coop::element::Registry::Get().EidForActor(actor);
    return e == coop::element::kInvalidId ? 0u : static_cast<uint32_t>(e);
}

void* ActorOf(uint32_t eid) {
    return eid ? coop::element::LivePropActor(static_cast<coop::element::ElementId>(eid)) : nullptr;
}

// A live trigger's `buf`: TArray<UPrimitiveComponent*>, read in place.
struct Buf {
    void* const* data = nullptr;
    int32_t num = 0;
};

Buf BufOf(void* trigger) {
    static int32_t s_off = -2;
    if (s_off == -2) {
        s_off = R::FindPropertyOffset(R::ClassOf(trigger), L"buf");
        if (s_off < 0) UE_LOGW("agrav_sync: trigger_agrav_C has no `buf` -- the float is not followed");
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

void Send(bool lifted, const std::vector<uint32_t>& eids) {
    if (!g_session || eids.empty()) return;
    for (size_t at = 0; at < eids.size(); at += N::kAgravEidsMax) {
        N::AgravStatePayload p{};
        p.lifted = lifted ? 1 : 0;
        const size_t n = (eids.size() - at < static_cast<size_t>(N::kAgravEidsMax)) ? eids.size() - at
                                                                                    : static_cast<size_t>(N::kAgravEidsMax);
        p.count = static_cast<uint16_t>(n);
        std::memcpy(p.eid, eids.data() + at, n * sizeof(uint32_t));
        if (!g_session->SendReliable(N::ReliableKind::AgravState, &p, sizeof(p)))
            UE_LOGW("agrav_sync: an AgravState of %zu prop(s) was not sent", n);
    }
    (lifted ? g_rowsLifted : g_rowsDropped) += eids.size();
    UE_LOGI("agrav_sync: told %zu prop(s) %s (%llu lifted, %llu dropped in all)", eids.size(),
            lifted ? "lifted" : "dropped", static_cast<unsigned long long>(g_rowsLifted),
            static_cast<unsigned long long>(g_rowsDropped));
}

void HostScan() {
    const std::vector<void*> triggers = Triggers();
    bool fresh = false;
    std::vector<uint32_t> lift, drop;
    for (void* t : triggers) {
        const Buf b = BufOf(t);
        if (b.data != g_lastBuf || b.num != g_lastNum) {
            g_lastBuf = b.data;
            g_lastNum = b.num;
            fresh = true;
        }
        for (int32_t i = 0; i < b.num; ++i) {
            void* const comp = b.data[i];
            if (!comp || !R::IsLive(comp)) continue;
            const uint32_t eid = EidOf(R::OuterOf(comp));
            if (!eid || g_lifted.count(eid)) continue;
            if (!E::IsComponentGravityEnabled(comp) && E::IsPrimitiveComponent(comp)) {
                g_lifted[eid] = true;
                lift.push_back(eid);
            }
        }
    }
    for (auto it = g_lifted.begin(); it != g_lifted.end();) {
        void* const actor = ActorOf(it->first);
        void* const root = actor ? E::RootComponentOf(actor) : nullptr;
        if (!root || E::IsComponentGravityEnabled(root)) {
            if (root) drop.push_back(it->first);
            it = g_lifted.erase(it);
        } else {
            ++it;
        }
    }
    Send(true, lift);
    Send(false, drop);
    const uint64_t now = Now();
    if (fresh || !lift.empty() || !drop.empty() || !g_lifted.empty() || !g_quietSince) g_quietSince = now;
}

}  // namespace

void Install(N::Session* session) { g_session = session; }

void Tick() {
    if (!g_session || g_session->role() != N::Role::Host || !g_session->connected()) return;
    const uint64_t now = Now();
    if (now < g_nextScan) return;
    // Quiet (nothing told is off and the buffers have not changed for a while): look once a second for a new gather.
    const bool quiet = g_lifted.empty() && g_quietSince && now - g_quietSince > kQuietMs;
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
    int done = 0, missing = 0, simulating = 0;
    for (int i = 0; i < p.count && i < N::kAgravEidsMax; ++i) {
        void* const actor = ActorOf(p.eid[i]);
        void* const root = actor ? E::RootComponentOf(actor) : nullptr;
        if (!root || !E::IsPrimitiveComponent(root)) { ++missing; continue; }
        if (E::IsComponentSimulatingPhysics(root)) ++simulating;
        E::SetComponentGravity(root, !lifted);
        if (lifted) {
            E::SetComponentLinearVelocity(root, 0.f, 0.f, kLiftCmPerSec);
        } else {
            coop::prop_rest_sync::NoteLocalMove(actor);
        }
        ++done;
    }
    g_applied += done;
    g_missing += missing;
    UE_LOGI("agrav_sync: the host %s %d prop(s) here (%d simulating, %d with no copy here)",
            lifted ? "lifted" : "dropped", done, simulating, missing);
}

void OnDisconnect() {
    g_lifted.clear();
    g_lastBuf = nullptr;
    g_lastNum = 0;
    g_quietSince = 0;
    g_nextScan = 0;
    if (g_rowsLifted || g_applied)
        UE_LOGI("agrav_sync: session over -- told %llu lifted and %llu dropped; applied %llu, %llu with no copy",
                static_cast<unsigned long long>(g_rowsLifted), static_cast<unsigned long long>(g_rowsDropped),
                static_cast<unsigned long long>(g_applied), static_cast<unsigned long long>(g_missing));
    g_rowsLifted = g_rowsDropped = g_applied = g_missing = 0;
}

}  // namespace coop::agrav_sync
