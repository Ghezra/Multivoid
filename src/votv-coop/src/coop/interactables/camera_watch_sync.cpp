// coop/interactables/camera_watch_sync.cpp -- see coop/interactables/camera_watch_sync.h.
//
// 0.9.0n. prop_camera_bad_C (and its good, ultra, cursed and s children): upd picks the LED -- red while not `active`,
// blue while `active` and not `spec`, green while both -- and shows the spotlight by `isLight`. ui_laptop_C::setCam
// clears `spec` on the camera it held and sets it on the one picked, and points that camera's capture at its screen; a
// drone pick sets `cam` to nothing and clears nothing. Its switcher shows the cameras panel as `canvas_cams`.

#include "coop/interactables/camera_watch_sync.h"

#include "coop/interactables/interactable_sync.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/devices/security_camera.h"
#include "ue_wrap/facts/function_row.h"

#include <windows.h>

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>

namespace coop::camera_watch_sync {
namespace {

namespace N  = coop::net;
namespace sg = ue_wrap::script_gate;
namespace SC = ue_wrap::security_camera;

UE_FACT_FN(kRowSetCam, "camera_watch_sync", L"ui_laptop_C", L"setCam", Declared);
constexpr int kTag = 0x43575301;  // 'CWS'
constexpr uint64_t kLookMs = 250;

N::Session* g_session = nullptr;
bool g_watched = false, g_settled = false;
uint64_t g_nextLook = 0;
std::wstring g_mine;            // the camera this peer's player watches ("" none), as last sent or kept
bool g_mineKnown = false;

// HOST: each slot's watched camera, and the set of cameras sent watched.
std::wstring g_slotCam[N::kMaxPeers];
std::set<std::wstring> g_watchedSent;
// CLIENT: the host's watched cameras.
std::set<std::wstring> g_hostWatched;
uint32_t g_asks = 0, g_states = 0, g_applied = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }

// A camera's `spec` written and its light painted, when it differs.
bool Paint(const std::wstring& key, bool on) {
    void* const cam = coop::interactable_sync::FindCamera(key);
    bool cur = false;
    if (!cam || !SC::ReadWatched(cam, cur)) return false;
    if (cur == on) return true;
    SC::WriteWatched(cam, on);
    SC::CallUpd(cam);
    return true;
}

void SendState(const std::wstring& key, bool on, int slot) {
    N::CameraWatchStatePayload p{};
    N::WireKeyFromString(key, p.key);
    p.watched = on ? 1 : 0;
    const bool ok = slot >= 0 ? g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::CameraWatchState,
                                                              &p, sizeof(p))
                              : g_session->SendReliable(N::ReliableKind::CameraWatchState, &p, sizeof(p));
    if (ok) ++g_states;
}

// HOST: the cameras any player watches, against those sent: each change painted here and sent.
void HostRecompute() {
    std::set<std::wstring> now;
    for (const std::wstring& k : g_slotCam)
        if (!k.empty()) now.insert(k);
    for (const std::wstring& k : now)
        if (!g_watchedSent.count(k)) {
            Paint(k, true);
            SendState(k, true, -1);
            UE_LOGI("camera_watch_sync: HOST camera '%ls' is watched", k.c_str());
        }
    for (const std::wstring& k : g_watchedSent)
        if (!now.count(k)) {
            Paint(k, false);
            SendState(k, false, -1);
            UE_LOGI("camera_watch_sync: HOST camera '%ls' is watched by nobody", k.c_str());
        }
    g_watchedSent.swap(now);
}

// The session's bits written back over what this machine's own laptop's setCam wrote: on a client the host's, on the
// host the set it sent. A camera its own pick set that the set does not name stands unwatched until the set does.
void Repaint() {
    const std::set<std::wstring>& set = IsHost() ? g_watchedSent : g_hostWatched;
    for (const std::wstring& k : set) Paint(k, true);
    if (void* const cam = SC::LocalWatchedCamera()) {
        const std::wstring k = coop::interactable_sync::CameraKeyOf(cam);
        if (!k.empty() && !set.count(k)) Paint(k, false);
    }
}

void OnSetCamPost(const sg::Call&) {
    if (IsHost() || IsClient()) Repaint();
    g_nextLook = 0;   // what this player watches is looked at again at once
}

void Look() {
    void* const cam = SC::LocalWatchedCamera();
    const std::wstring key = cam ? coop::interactable_sync::CameraKeyOf(cam) : std::wstring();
    if (g_mineKnown && key == g_mine) return;
    g_mine = key;
    g_mineKnown = true;
    if (IsHost()) {
        g_slotCam[0] = key;
        HostRecompute();
        return;
    }
    N::CameraWatchAskPayload p{};
    N::WireKeyFromString(key, p.key);
    if (g_session->SendReliable(N::ReliableKind::CameraWatchAsk, &p, sizeof(p))) ++g_asks;
    UE_LOGI("camera_watch_sync: CLIENT watches %ls%ls%ls", key.empty() ? L"no camera" : L"camera '", key.c_str(),
            key.empty() ? L"" : L"'");
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    if (!sg::WatchName(kRowSetCam, kTag, nullptr, &OnSetCamPost))
        UE_LOGE("camera_watch_sync: the watch on the laptop's setCam was refused");
}

void Tick() {
    if (g_watched && !g_settled) {
        sg::ResolvePendingNames();
        if (sg::NameWatchSettled(kRowSetCam, kTag)) {
            g_settled = true;
            if (sg::NameWatchLive(kRowSetCam, kTag)) UE_LOGI("camera_watch_sync: the watch on the laptop's setCam is live");
            else UE_LOGE("camera_watch_sync: the setCam watch is not live -- a client's own pick shows until the host's next bit");
        }
    }
    if (!g_session || !g_session->connected() || !SC::EnsureResolved()) return;
    const uint64_t now = ::GetTickCount64();
    if (now < g_nextLook) return;
    g_nextLook = now + kLookMs;
    Look();
    // A watched camera this client could not name when the host's bit came (a joiner's Kerfur camera before its Omega's
    // element id) is painted once it can; one already painted is left alone.
    if (IsClient())
        for (const std::wstring& k : g_hostWatched) Paint(k, true);
}

void OnAsk(int senderSlot, const N::CameraWatchAskPayload& p) {
    if (!IsHost() || senderSlot <= 0 || senderSlot >= static_cast<int>(N::kMaxPeers)) return;
    const std::wstring key = N::StringFromWireKey(p.key);
    if (!key.empty() && !coop::interactable_sync::FindCamera(key)) {
        UE_LOGW("camera_watch_sync: HOST slot %d watches camera '%ls', which this world does not hold", senderSlot,
                key.c_str());
        return;
    }
    g_slotCam[senderSlot] = key;
    HostRecompute();
}

void OnState(const N::CameraWatchStatePayload& p) {
    if (!IsClient()) return;
    const std::wstring key = N::StringFromWireKey(p.key);
    if (key.empty()) return;
    if (p.watched) g_hostWatched.insert(key);
    else g_hostWatched.erase(key);
    ++g_applied;
    if (!Paint(key, p.watched != 0))
        UE_LOGW("camera_watch_sync: CLIENT camera '%ls' (%s) is not here to paint", key.c_str(),
                p.watched ? "watched" : "unwatched");
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    for (const std::wstring& k : g_watchedSent) SendState(k, true, slot);
}

void OnPeerLeftSlot(int slot) {
    if (!IsHost() || slot <= 0 || slot >= static_cast<int>(N::kMaxPeers) || g_slotCam[slot].empty()) return;
    g_slotCam[slot].clear();
    HostRecompute();
}

void OnDisconnect() {
    if (g_asks || g_states || g_applied)
        UE_LOGI("camera_watch_sync: session end -- client watch reports %u, host states sent %u, client states applied %u",
                g_asks, g_states, g_applied);
    for (std::wstring& k : g_slotCam) k.clear();
    g_watchedSent.clear();
    g_hostWatched.clear();
    g_mine.clear();
    g_mineKnown = false;
    g_nextLook = 0;
    g_asks = g_states = g_applied = 0;
}

}  // namespace coop::camera_watch_sync
