// coop/interactables/door_dent_sync.cpp -- see coop/interactables/door_dent_sync.h.
//
// 0.9.0n. door_C::addDamage returns at once while `isMoving`, or for a damage of 1 or less, or a `damageProtected` door;
// else it moves `door_r_root` toward -opendoor and `door_l_root` toward +opendoor by a step of the damage, and calls
// doorOpen(true) once the right leaf is more than 90 cm from opendoor and the door is not open. doorOpen's swing starts
// from where the leaves stand (lerpA_r, lerpA_l).

#include "coop/interactables/door_dent_sync.h"

#include "coop/interactables/interactable_sync.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/facts/function_row.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace coop::door_dent_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;

UE_FACT_FN(kRowAddDamage, "door_dent_sync", L"door_C", L"addDamage", Declared);
constexpr int kTag = 0x44445301;  // 'DDS'
constexpr float kMovedCm = 0.5f;

N::Session* g_session = nullptr;
bool g_watched = false, g_settled = false;
struct Leaves {
    float r[3], l[3];
};
std::unordered_map<std::wstring, Leaves> g_sent;  // HOST: each door sent, as last sent
uint32_t g_states = 0, g_applied = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }

void* Field(void* o, const wchar_t* name) {
    const int32_t off = o ? R::FindPropertyOffset(R::ClassOf(o), name) : -1;
    return off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(o) + off) : nullptr;
}

bool Moving(void* door) {
    const int32_t off = door ? R::FindPropertyOffset(R::ClassOf(door), L"isMoving") : -1;
    return off >= 0 && *(static_cast<const uint8_t*>(door) + off) != 0;
}

bool Read(void* door, Leaves& out) {
    void* const r = Field(door, L"door_r_root");
    void* const l = Field(door, L"door_l_root");
    if (!r || !l) return false;
    const ue_wrap::FVector a = ue_wrap::engine::GetComponentRelativeLocation(r);
    const ue_wrap::FVector b = ue_wrap::engine::GetComponentRelativeLocation(l);
    out = {{a.X, a.Y, a.Z}, {b.X, b.Y, b.Z}};
    return true;
}

void SetRelLoc(void* comp, const float v[3]) {
    static void* fn = nullptr;
    if (!fn)
        if (void* const sc = R::FindClass(L"SceneComponent")) fn = R::FindFunction(sc, L"K2_SetRelativeLocation");
    ue_wrap::ParamFrame f(fn);
    if (!f.valid()) return;
    f.Set<ue_wrap::FVector>(L"NewLocation", ue_wrap::FVector{v[0], v[1], v[2]});
    f.Set<bool>(L"bTeleport", true);
    ue_wrap::Call(comp, f);
}

bool Differs(const Leaves& a, const Leaves& b) {
    for (int i = 0; i < 3; ++i)
        if (std::fabs(a.r[i] - b.r[i]) > kMovedCm || std::fabs(a.l[i] - b.l[i]) > kMovedCm) return true;
    return false;
}

void Send(const std::wstring& key, const Leaves& v, int slot) {
    N::DoorDentStatePayload p{};
    N::WireKeyFromString(key, p.key);
    for (int i = 0; i < 3; ++i) {
        p.right[i] = v.r[i];
        p.left[i] = v.l[i];
    }
    const bool ok = slot >= 0
        ? g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::DoorDentState, &p, sizeof(p))
        : g_session->SendReliable(N::ReliableKind::DoorDentState, &p, sizeof(p));
    if (ok) ++g_states;
}

void OnAddDamagePost(const sg::Call& call) {
    if (!IsHost() || !call.object) return;
    const std::wstring key = coop::interactable_sync::DoorKey(call.object);
    Leaves now{};
    if (key.empty() || !Read(call.object, now)) return;
    const auto it = g_sent.find(key);
    if (it != g_sent.end() && !Differs(it->second, now)) return;
    g_sent[key] = now;
    Send(key, now, -1);
    UE_LOGI("door_dent_sync: HOST door '%ls' leaves at R(%.0f,%.0f,%.0f) L(%.0f,%.0f,%.0f)", key.c_str(), now.r[0],
            now.r[1], now.r[2], now.l[0], now.l[1], now.l[2]);
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    if (!sg::WatchName(kRowAddDamage, kTag, nullptr, &OnAddDamagePost))
        UE_LOGE("door_dent_sync: the watch on the door's addDamage was refused");
}

void Tick() {
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowAddDamage, kTag)) return;
    g_settled = true;
    if (sg::NameWatchLive(kRowAddDamage, kTag)) UE_LOGI("door_dent_sync: the watch on the door's addDamage is live");
    else UE_LOGE("door_dent_sync: the addDamage watch is not live -- a hit door's leaves are not sent");
}

void OnState(const N::DoorDentStatePayload& p) {
    if (!IsClient()) return;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(p.right[i]) || !std::isfinite(p.left[i])) return;
    const std::wstring key = N::StringFromWireKey(p.key);
    void* const door = key.empty() ? nullptr : coop::interactable_sync::ResolveDoor(key);
    if (!door) {
        UE_LOGW("door_dent_sync: CLIENT door '%ls' is not here", key.c_str());
        return;
    }
    if (Moving(door)) return;
    void* const r = Field(door, L"door_r_root");
    void* const l = Field(door, L"door_l_root");
    if (!r || !l) return;
    SetRelLoc(r, p.right);
    SetRelLoc(l, p.left);
    ++g_applied;
    UE_LOGI("door_dent_sync: CLIENT door '%ls' leaves set to the host's", key.c_str());
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    for (const auto& kv : g_sent) {
        void* const door = coop::interactable_sync::ResolveDoor(kv.first);
        Leaves now{};
        if (door && !Moving(door) && Read(door, now)) Send(kv.first, now, slot);
    }
}

void OnDisconnect() {
    if (g_states || g_applied)
        UE_LOGI("door_dent_sync: session end -- host states sent %u, client states applied %u", g_states, g_applied);
    g_sent.clear();
    g_states = g_applied = 0;
}

}  // namespace coop::door_dent_sync
