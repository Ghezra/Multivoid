// coop/interactables/camera_aim_sync.cpp -- see coop/interactables/camera_aim_sync.h.
//
// 0.9.0n. prop_camera_bad_C (and its children): setAngle(rot_x, rot_y, zoom) writes the three targets (only `zoom` while
// `ignoreMove`) and runs move; addAngle adds to each and runs move. move turns `head` toward (rot_y, rot_x) and both
// lenses' FOV toward `zoom` a step a frame, clamping the targets to clamp_x, clamp_y and clamp_z, and plays its motor
// sounds while turning. loadData reads rot_x and rot_y from the save and runs setRot.

#include "coop/interactables/camera_aim_sync.h"

#include "coop/interactables/interactable_sync.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace coop::camera_aim_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;

UE_FACT_FN(kRowSetAngle, "camera_aim_sync", L"prop_camera_bad_C", L"setAngle", Declared);
UE_FACT_FN(kRowAddAngle, "camera_aim_sync", L"prop_camera_bad_C", L"addAngle", Declared);
constexpr int kTagSet = 0x43414D01, kTagAdd = 0x43414D02;  // 'CAM'
constexpr uint64_t kSendMs = 100;

N::Session* g_session = nullptr;
bool g_watched = false, g_settled = false;
bool g_applying = false;

struct Aim {
    float x = 0.f, y = 0.f, zoom = 0.f;
};
// A camera turned here and not yet sent: its key, and on the host the slot whose ask it was (-1 its own).
struct Dirty {
    int fromSlot = -1;
};
std::unordered_map<std::wstring, Dirty> g_dirty;
std::unordered_map<std::wstring, uint64_t> g_lastSent;
std::unordered_map<std::wstring, Aim> g_turned;   // HOST: every camera turned this session, as last sent
// CLIENT: a host's aim for a camera this client cannot name yet (a joiner's Kerfur camera before its Omega's element
// id), tried again each look until it can, for up to kPendingMs.
struct PendingAim {
    N::CameraAimPayload p;
    uint64_t until;
};
std::unordered_map<std::wstring, PendingAim> g_pending;
constexpr uint64_t kPendingMs = 60000;
uint64_t g_nextRetry = 0;
uint32_t g_asks = 0, g_states = 0, g_applied = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }

float* FloatField(void* cam, const wchar_t* name) {
    const int32_t off = cam ? R::FindPropertyOffset(R::ClassOf(cam), name) : -1;
    return off >= 0 ? reinterpret_cast<float*>(static_cast<char*>(cam) + off) : nullptr;
}

bool Read(void* cam, Aim& out) {
    const float* x = FloatField(cam, L"rot_x");
    const float* y = FloatField(cam, L"rot_y");
    const float* z = FloatField(cam, L"zoom");
    if (!x || !y || !z) return false;
    out = {*x, *y, *z};
    return true;
}

bool Apply(void* cam, const Aim& a) {
    float* x = FloatField(cam, L"rot_x");
    float* y = FloatField(cam, L"rot_y");
    float* z = FloatField(cam, L"zoom");
    if (!x || !y || !z) return false;
    *x = a.x;
    *y = a.y;
    *z = a.zoom;
    ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(cam), L"move"));
    g_applying = true;
    const bool ok = f.valid() && ue_wrap::Call(cam, f);
    g_applying = false;
    return ok;
}

// CLIENT: the last time this player turned each camera. A host's aim that arrives within kOwnTurnMs of it is an echo
// of a turn still going: it is kept and taken once the turning stops, so the head does not jump back mid-turn.
constexpr uint64_t kOwnTurnMs = 400;
std::unordered_map<std::wstring, uint64_t> g_ownTurnAt;
std::unordered_map<std::wstring, N::CameraAimPayload> g_held;

void OnAnglePost(const sg::Call& call) {
    if (g_applying || !call.object || !(IsHost() || IsClient())) return;
    const std::wstring key = coop::interactable_sync::CameraKeyOf(call.object);
    if (key.empty()) return;
    g_dirty.emplace(key, Dirty{});
    if (IsClient()) g_ownTurnAt[key] = ::GetTickCount64();
}

N::CameraAimPayload PayloadOf(const std::wstring& key, const Aim& a) {
    N::CameraAimPayload p{};
    N::WireKeyFromString(key, p.key);
    p.rotX = a.x;
    p.rotY = a.y;
    p.zoom = a.zoom;
    return p;
}

void Flush() {
    const uint64_t now = ::GetTickCount64();
    for (auto it = g_dirty.begin(); it != g_dirty.end();) {
        uint64_t& last = g_lastSent[it->first];
        if (now - last < kSendMs) {
            ++it;
            continue;
        }
        void* const cam = coop::interactable_sync::FindCamera(it->first);
        Aim a{};
        if (cam && Read(cam, a)) {
            const N::CameraAimPayload p = PayloadOf(it->first, a);
            if (IsHost()) {
                // To every client, the turner too: two players turning one camera at once each end on the host's
                // last turn, which the turner takes once its own turning stops (OnState).
                if (g_session->SendReliable(N::ReliableKind::CameraAimState, &p, sizeof(p))) ++g_states;
                g_turned[it->first] = a;
            } else if (g_session->SendReliable(N::ReliableKind::CameraAimAsk, &p, sizeof(p))) {
                ++g_asks;
            }
            last = now;
        }
        it = g_dirty.erase(it);
    }
}

bool Finite(const N::CameraAimPayload& p) {
    return std::isfinite(p.rotX) && std::isfinite(p.rotY) && std::isfinite(p.zoom) && std::fabs(p.rotX) < 1e5f &&
           std::fabs(p.rotY) < 1e5f && std::fabs(p.zoom) < 1e5f;
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    const bool a = sg::WatchName(kRowSetAngle, kTagSet, nullptr, &OnAnglePost);
    const bool b = sg::WatchName(kRowAddAngle, kTagAdd, nullptr, &OnAnglePost);
    if (!a || !b) UE_LOGE("camera_aim_sync: a watch on the camera's setAngle or addAngle was refused");
}

void Tick() {
    if (g_watched && !g_settled) {
        sg::ResolvePendingNames();
        if (sg::NameWatchSettled(kRowSetAngle, kTagSet) && sg::NameWatchSettled(kRowAddAngle, kTagAdd)) {
            g_settled = true;
            if (sg::NameWatchLive(kRowSetAngle, kTagSet) && sg::NameWatchLive(kRowAddAngle, kTagAdd))
                UE_LOGI("camera_aim_sync: the watches on the camera's setAngle and addAngle are live");
            else
                UE_LOGE("camera_aim_sync: a camera angle watch is not live -- a turn shows on its own machine alone");
        }
    }
    if (!g_session || !g_session->connected()) return;
    const uint64_t now = ::GetTickCount64();
    if (!g_pending.empty() && now >= g_nextRetry) {
        g_nextRetry = now + 500;
        for (auto it = g_pending.begin(); it != g_pending.end();) {
            void* const cam = coop::interactable_sync::FindCamera(it->first);
            if (cam && Apply(cam, {it->second.p.rotX, it->second.p.rotY, it->second.p.zoom})) {
                UE_LOGI("camera_aim_sync: CLIENT camera '%ls' turned to the host's once it could be named",
                        it->first.c_str());
                ++g_applied;
                it = g_pending.erase(it);
            } else if (now > it->second.until) {
                UE_LOGW("camera_aim_sync: CLIENT camera '%ls' never came to be named -- the host's aim dropped",
                        it->first.c_str());
                it = g_pending.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto it = g_held.begin(); it != g_held.end();) {
        const auto own = g_ownTurnAt.find(it->first);
        if (own != g_ownTurnAt.end() && now - own->second < kOwnTurnMs) {
            ++it;
            continue;
        }
        const N::CameraAimPayload p = it->second;
        it = g_held.erase(it);
        if (void* const cam = coop::interactable_sync::FindCamera(N::StringFromWireKey(p.key)))
            if (Apply(cam, {p.rotX, p.rotY, p.zoom})) ++g_applied;
    }
    if (!g_dirty.empty()) Flush();
}

void OnAsk(int senderSlot, const N::CameraAimPayload& p) {
    if (!IsHost() || senderSlot <= 0 || senderSlot >= static_cast<int>(N::kMaxPeers) || !Finite(p)) return;
    const std::wstring key = N::StringFromWireKey(p.key);
    void* const cam = key.empty() ? nullptr : coop::interactable_sync::FindCamera(key);
    if (!cam) {
        UE_LOGW("camera_aim_sync: HOST slot %d turned camera '%ls', which this world does not hold", senderSlot,
                key.c_str());
        return;
    }
    if (!Apply(cam, {p.rotX, p.rotY, p.zoom})) return;
    g_dirty[key] = Dirty{senderSlot};
    if (g_asks == 0 && g_applied == 0)
        UE_LOGI("camera_aim_sync: HOST slot %d turned camera '%ls' to (%.1f, %.1f) zoom %.1f", senderSlot, key.c_str(),
                p.rotX, p.rotY, p.zoom);
    ++g_applied;
}

void OnState(const N::CameraAimPayload& p) {
    if (!IsClient() || !Finite(p)) return;
    const std::wstring key = N::StringFromWireKey(p.key);
    const auto own = g_ownTurnAt.find(key);
    if (own != g_ownTurnAt.end() && ::GetTickCount64() - own->second < kOwnTurnMs) {
        g_held[key] = p;
        return;
    }
    g_held.erase(key);
    void* const cam = key.empty() ? nullptr : coop::interactable_sync::FindCamera(key);
    if (!cam) {
        if (!g_pending.count(key))
            UE_LOGI("camera_aim_sync: CLIENT camera '%ls' is not here yet -- the host's aim waits for it", key.c_str());
        g_pending[key] = PendingAim{p, ::GetTickCount64() + kPendingMs};
        return;
    }
    g_pending.erase(key);
    if (!Apply(cam, {p.rotX, p.rotY, p.zoom})) return;
    if (g_applied == 0)
        UE_LOGI("camera_aim_sync: CLIENT camera '%ls' turned to the host's (%.1f, %.1f) zoom %.1f", key.c_str(), p.rotX,
                p.rotY, p.zoom);
    ++g_applied;
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    for (const auto& kv : g_turned) {
        void* const cam = coop::interactable_sync::FindCamera(kv.first);
        Aim a{};
        if (!cam || !Read(cam, a)) continue;
        const N::CameraAimPayload p = PayloadOf(kv.first, a);
        if (g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::CameraAimState, &p, sizeof(p)))
            ++g_states;
    }
}

void OnDisconnect() {
    if (g_asks || g_states || g_applied)
        UE_LOGI("camera_aim_sync: session end -- client turns sent %u, host states sent %u, turns applied %u", g_asks,
                g_states, g_applied);
    g_dirty.clear();
    g_lastSent.clear();
    g_turned.clear();
    g_pending.clear();
    g_held.clear();
    g_ownTurnAt.clear();
    g_asks = g_states = g_applied = 0;
}

}  // namespace coop::camera_aim_sync
