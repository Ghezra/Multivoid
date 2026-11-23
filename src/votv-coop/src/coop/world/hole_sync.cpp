// coop/world/hole_sync.cpp -- see coop/world/hole_sync.h.
//
// 0.9.0n. event_bottomHoleController_C's box, while its `room` is unset: the room (holeBottom_C) at a fixed place, the
// device (holeDevice_C, static), the patrol controller (kavotiaPatrolController_C, `eventObject` itself); the save's
// `holegrate` replaced by a static prop_C named holegrate_0, the save's `holefence` freed and pushed; four
// prop_rozitalBeacon_C traced down around the device. Its end destroys the device, the patrol and the beacons.

#include "coop/world/hole_sync.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"

#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace coop::hole_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;

constexpr const wchar_t* kRoom = L"holeBottom_C";
constexpr const wchar_t* kController = L"event_bottomHoleController_C";
UE_FACT_FN(kRowBox, "hole_sync", L"event_bottomHoleController_C",
           L"BndEvt__event_bottomHoleController_Box_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature",
           Declared);
constexpr int kTagBox = 0x484F4C58;   // 'HOLX'
constexpr uint64_t kRoomMs = 5000, kLiveStaleMs = 15000;

std::atomic<N::Session*> g_session{nullptr};
bool g_box = false;
uint64_t g_nextRoom = 0;
uint64_t g_connectedAt = 0, g_liveAt = 0;   // CLIENT: this session's first connected tick; the host's last 118

// [dev] week6_red=hole_host, the week six drill's red arm: the Hole opens on the host alone, as before.
bool RedHost() {
    static const bool s_red = coop::config::ResolveEnum(coop::config_registry::rows::week6_red) == "hole_host";
    return s_red;
}

bool IsClient() {
    N::Session* const s = g_session.load(std::memory_order_acquire);
    return s && s->role() == N::Role::Client;
}

// CLIENT: the controller's box opens the Hole on the host alone.
sg::Verdict OnBoxPre(const sg::Call& call) {
    return call.tag == kTagBox && IsClient() && !RedHost() ? sg::Verdict::Cancel : sg::Verdict::Run;
}

void SendRooms(N::Session& s) {
    void* rooms[4];
    const int32_t n = ue_wrap::world_instances::Find(kRoom, rooms, 4);
    for (int32_t i = 0; i < n; ++i) {
        ue_wrap::FVector at{};
        if (!rooms[i] || !R::IsLive(rooms[i]) || !ue_wrap::engine::TryGetActorLocation(rooms[i], at)) continue;
        N::EventCuePayload p{};
        p.cueId = kCueHoleRoom;
        p.x = at.X;
        p.y = at.Y;
        p.z = at.Z;
        s.SendReliable(N::ReliableKind::EventCue, &p, sizeof(p));
    }
    void* ctl[4];
    if (ue_wrap::world_instances::Find(kController, ctl, 4) > 0 && ctl[0] && R::IsLive(ctl[0])) {
        N::EventCuePayload p{};
        p.cueId = kCueHoleLive;
        s.SendReliable(N::ReliableKind::EventCue, &p, sizeof(p));
    }
}

// CLIENT: a controller this machine loaded from its save outlives the host's, which ended; it ends too.
void EndStaleControllers(uint64_t now) {
    if (now - g_connectedAt < kLiveStaleMs || now - g_liveAt < kLiveStaleMs) return;
    void* ctl[4];
    const int32_t n = ue_wrap::world_instances::Find(kController, ctl, 4);
    for (int32_t i = 0; i < n; ++i)
        if (ctl[i] && R::IsLive(ctl[i]) && ue_wrap::engine::DestroyActor(ctl[i]))
            UE_LOGI("hole: this machine's own Hole controller from its save ended -- the host has none");
}

void MakeRoom(const N::EventCuePayload& p) {
    void* rooms[4];
    const int32_t n = ue_wrap::world_instances::Find(kRoom, rooms, 4);
    for (int32_t i = 0; i < n; ++i) {
        ue_wrap::FVector at{};
        if (!rooms[i] || !R::IsLive(rooms[i]) || !ue_wrap::engine::TryGetActorLocation(rooms[i], at)) continue;
        const float dx = at.X - p.x, dy = at.Y - p.y, dz = at.Z - p.z;
        if (dx * dx + dy * dy + dz * dz < 500.f * 500.f) return;   // this machine has it (a joiner's, from its save)
    }
    void* cls = ue_wrap::object_index::ClassByName(kRoom);
    if (!cls) cls = R::FindClass(kRoom);
    // Never spawned in this world, its class is not loaded yet: the game's own load of it.
    if (!cls) cls = ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/holeBottom.holeBottom_C");
    const ue_wrap::FVector at{p.x, p.y, p.z};
    void* const room = cls ? ue_wrap::engine::SpawnActor(cls, at) : nullptr;
    UE_LOGI("hole: the host's Hole is open, and its bottom room is made here at (%.0f,%.0f,%.0f) (%s)", p.x, p.y, p.z,
            room ? "made" : cls ? "spawn failed" : "class not found");
}

}  // namespace

void Install(N::Session* session) { g_session.store(session, std::memory_order_release); }

void Tick() {
    N::Session* const s = g_session.load(std::memory_order_acquire);
    if (!s || !s->running() || RedHost()) return;
    if (!g_box && (g_box = sg::WatchName(kRowBox, kTagBox, &OnBoxPre, nullptr)))
        UE_LOGI("hole: the Hole controller's box is watched");
    if (!s->connected()) return;
    const uint64_t now = ::GetTickCount64();
    if (now < g_nextRoom) return;
    g_nextRoom = now + kRoomMs;
    if (s->role() == N::Role::Host) {
        SendRooms(*s);
        return;
    }
    if (!g_connectedAt) g_connectedAt = now;
    EndStaleControllers(now);
}

bool OnCue(const N::EventCuePayload& p) {
    if (p.cueId != kCueHoleRoom && p.cueId != kCueHoleLive) return false;
    if (!IsClient() || RedHost()) return true;
    if (p.cueId == kCueHoleLive) g_liveAt = ::GetTickCount64();
    else MakeRoom(p);
    return true;
}

void OnDisconnect() {
    g_nextRoom = g_connectedAt = g_liveAt = 0;
    g_session.store(nullptr, std::memory_order_release);
}

}  // namespace coop::hole_sync
