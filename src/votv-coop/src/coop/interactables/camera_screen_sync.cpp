// coop/interactables/camera_screen_sync.cpp -- see coop/interactables/camera_screen_sync.h.
//
// 0.9.0n. uicomp_camSlot_C's name box commit writes its `cam`'s `cameraName` (an empty box keeps the old name) and
// shows it. ui_tvInterface_C's search box commit runs its TV's (`radio`) bindCamera(FName of the text), which walks
// gamemode.allSecurityCameras (every camera adds itself at its begin play) for that `cameraName`, sets `bindedCamera`
// and points the camera's captures at a new render target the TV's security material shows, then setAsSecurity(found):
// it says "no update" and returns when `isSecurity` is that already, else sets it, the material, and the surveillance
// ticker's list. The TV saves neither.

#include "coop/interactables/camera_screen_sync.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/interactables/interactable_sync.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace coop::camera_screen_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace EL = coop::element;
namespace sg = ue_wrap::script_gate;
namespace FI = ue_wrap::field_io;

UE_FACT_FN(kRowSlotCommit, "camera_screen_sync", L"uicomp_camSlot_C",
           L"BndEvt__umg_camSlot_EditableTextBox_342_K2Node_ComponentBoundEvent_2_OnEditableTextBoxCommittedEvent__DelegateSignature",
           Declared);
UE_FACT_FN(kRowSetAsSecurity, "camera_screen_sync", L"prop_tv2_C", L"setAsSecurity", Declared);
constexpr int kTagName = 0x43535301, kTagTv = 0x43535302;  // 'CSS'

N::Session* g_session = nullptr;
bool g_watched = false, g_settled = false;
bool g_applying = false;
std::unordered_map<std::wstring, std::wstring> g_names;   // HOST: camera key -> name, renamed this session
std::unordered_map<uint32_t, std::wstring> g_tvs;         // HOST: TV eid -> camera key ("" none), changed this session
// CLIENT: a host's name for a camera this client cannot name yet (a joiner's Kerfur camera before its Omega's element
// id), tried again each half second for up to a minute.
struct PendingName {
    std::wstring name;
    uint64_t until;
};
std::unordered_map<std::wstring, PendingName> g_pendingNames;
uint64_t g_nextRetry = 0;
uint32_t g_asks = 0, g_states = 0, g_applied = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }

void* ObjField(void* o, const wchar_t* name) {
    const int32_t off = o ? R::FindPropertyOffset(R::ClassOf(o), name) : -1;
    void* const v = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(o) + off) : nullptr;
    return v && R::IsLive(v) ? v : nullptr;
}

bool ReadName(void* cam, std::wstring& out) {
    const int32_t off = cam ? R::FindPropertyOffset(R::ClassOf(cam), L"cameraName") : -1;
    if (off < 0) return false;
    out = FI::ReadFStringAt(cam, off);
    return true;
}

bool WriteName(void* cam, const std::wstring& name) {
    const int32_t off = cam ? R::FindPropertyOffset(R::ClassOf(cam), L"cameraName") : -1;
    return off >= 0 && FI::WriteFStringField(cam, off, name);
}

bool ReadSecurity(void* tv) {
    int32_t off = -1;
    uint8_t mask = 0;
    return tv && R::FindBoolProperty(R::ClassOf(tv), L"isSecurity", off, mask) &&
           (static_cast<const uint8_t*>(tv)[off] & mask) != 0;
}

uint32_t EidOf(void* obj) {
    const EL::ElementId eid = EL::Registry::Get().EidForActor(obj);
    return eid == EL::kInvalidId ? 0u : static_cast<uint32_t>(eid);
}

void* TvOf(uint32_t eid) {
    return eid ? EL::LiveActorOfType(static_cast<EL::ElementId>(eid), EL::ElementType::Prop) : nullptr;
}

// The camera the TV shows ("" none): its bound camera while it is a security screen.
std::wstring TvCamKey(void* tv) {
    void* const cam = ReadSecurity(tv) ? ObjField(tv, L"bindedCamera") : nullptr;
    return cam ? coop::interactable_sync::CameraKeyOf(cam) : std::wstring();
}

N::CameraNamePayload NamePayload(const std::wstring& key, const std::wstring& name) {
    N::CameraNamePayload p{};
    N::WireKeyFromString(key, p.key);
    const size_t n = name.size() < N::kCameraNameMax ? name.size() : N::kCameraNameMax;
    p.len = static_cast<uint16_t>(n);
    for (size_t i = 0; i < n; ++i) p.name[i] = static_cast<uint16_t>(name[i]);
    return p;
}

std::wstring NameOfPayload(const N::CameraNamePayload& p) {
    std::wstring s;
    for (uint16_t i = 0; i < p.len && i < N::kCameraNameMax; ++i) s.push_back(static_cast<wchar_t>(p.name[i]));
    return s;
}

N::TvCameraPayload TvPayload(uint32_t eid, const std::wstring& camKey) {
    N::TvCameraPayload p{};
    p.tvEid = eid;
    N::WireKeyFromString(camKey, p.camKey);
    return p;
}

bool Send(N::ReliableKind kind, const void* p, int len, int skip) {
    const bool ok = g_session->SendReliable(kind, p, len, skip);
    if (ok) ++(IsHost() ? g_states : g_asks);
    return ok;
}

// The TV turned to the camera `camKey` ("" off) here, by this machine's own copy of the camera's name.
bool BindTv(void* tv, const std::wstring& camKey) {
    if (TvCamKey(tv) == camKey) return true;
    bool valid = false;
    g_applying = true;
    if (!camKey.empty()) {
        void* const cam = coop::interactable_sync::FindCamera(camKey);
        std::wstring name;
        if (cam && ReadName(cam, name)) {
            ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(tv), L"bindCamera"));
            const R::FName fname = ue_wrap::fname_utils::StringToFName(name);
            if (f.valid() && f.SetRaw(L"name", &fname, sizeof(fname)) && ue_wrap::Call(tv, f)) valid = f.Get<bool>(L"validCamera");
        }
    }
    if (valid != ReadSecurity(tv)) {
        ue_wrap::ParamFrame s(R::FindDispatchFunctionCached(R::ClassOf(tv), L"setAsSecurity"));
        if (s.valid() && s.Set<bool>(L"isSecurity", valid)) ue_wrap::Call(tv, s);
    }
    g_applying = false;
    return camKey.empty() ? !ReadSecurity(tv) : valid;
}

void OnCommitPost(const sg::Call& call) {
    if (g_applying || !(IsHost() || IsClient())) return;
    void* const cam = ObjField(call.object, L"cam");
    std::wstring name;
    const std::wstring key = cam ? coop::interactable_sync::CameraKeyOf(cam) : std::wstring();
    if (key.empty() || !ReadName(cam, name)) return;
    const N::CameraNamePayload p = NamePayload(key, name);
    if (IsHost()) {
        g_names[key] = name;
        Send(N::ReliableKind::CameraNameState, &p, sizeof(p), -1);
    } else {
        Send(N::ReliableKind::CameraNameAsk, &p, sizeof(p), -1);
    }
    UE_LOGI("camera_screen_sync: %s camera '%ls' named '%ls' -- sent", IsHost() ? "HOST" : "CLIENT", key.c_str(),
            name.c_str());
}

void OnSecurityPost(const sg::Call& call) {
    if (g_applying || !(IsHost() || IsClient()) || !call.object) return;
    const uint32_t eid = EidOf(call.object);
    if (!eid) return;
    const std::wstring camKey = TvCamKey(call.object);
    const N::TvCameraPayload p = TvPayload(eid, camKey);
    if (IsHost()) {
        g_tvs[eid] = camKey;
        Send(N::ReliableKind::TvCameraState, &p, sizeof(p), -1);
    } else {
        Send(N::ReliableKind::TvCameraAsk, &p, sizeof(p), -1);
    }
    UE_LOGI("camera_screen_sync: %s TV eid %u shows %ls%ls -- sent", IsHost() ? "HOST" : "CLIENT", eid,
            camKey.empty() ? L"no camera" : L"camera ", camKey.c_str());
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    const bool a = sg::WatchName(kRowSlotCommit, kTagName, nullptr, &OnCommitPost);
    const bool b = sg::WatchName(kRowSetAsSecurity, kTagTv, nullptr, &OnSecurityPost);
    if (!a || !b) UE_LOGE("camera_screen_sync: a watch on the camera slot's name or the TV's setAsSecurity was refused");
}

void Tick() {
    const uint64_t now = ::GetTickCount64();
    if (IsClient() && !g_pendingNames.empty() && now >= g_nextRetry) {
        g_nextRetry = now + 500;
        for (auto it = g_pendingNames.begin(); it != g_pendingNames.end();) {
            void* const cam = coop::interactable_sync::FindCamera(it->first);
            if (cam && WriteName(cam, it->second.name)) {
                ++g_applied;
                UE_LOGI("camera_screen_sync: CLIENT camera '%ls' named the host's '%ls' once it could be named",
                        it->first.c_str(), it->second.name.c_str());
                it = g_pendingNames.erase(it);
            } else if (now > it->second.until) {
                it = g_pendingNames.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowSlotCommit, kTagName) || !sg::NameWatchSettled(kRowSetAsSecurity, kTagTv)) return;
    g_settled = true;
    if (sg::NameWatchLive(kRowSlotCommit, kTagName) && sg::NameWatchLive(kRowSetAsSecurity, kTagTv))
        UE_LOGI("camera_screen_sync: the watches on the camera slot's name and the TV's setAsSecurity are live");
    else
        UE_LOGE("camera_screen_sync: a watch is not live -- a camera's name or a TV's camera stays on its machine");
}

void OnNameAsk(int senderSlot, const N::CameraNamePayload& p) {
    if (!IsHost() || senderSlot <= 0 || senderSlot >= static_cast<int>(N::kMaxPeers)) return;
    const std::wstring key = N::StringFromWireKey(p.key), name = NameOfPayload(p);
    void* const cam = key.empty() ? nullptr : coop::interactable_sync::FindCamera(key);
    if (!cam || name.empty() || !WriteName(cam, name)) {
        UE_LOGW("camera_screen_sync: HOST slot %d's name for camera '%ls' not taken", senderSlot, key.c_str());
        return;
    }
    g_names[key] = name;
    ++g_applied;
    Send(N::ReliableKind::CameraNameState, &p, sizeof(p), senderSlot);
    UE_LOGI("camera_screen_sync: HOST slot %d named camera '%ls' '%ls'", senderSlot, key.c_str(), name.c_str());
}

void OnNameState(const N::CameraNamePayload& p) {
    if (!IsClient()) return;
    const std::wstring key = N::StringFromWireKey(p.key), name = NameOfPayload(p);
    void* const cam = key.empty() ? nullptr : coop::interactable_sync::FindCamera(key);
    if (key.empty() || name.empty()) return;
    if (!cam || !WriteName(cam, name)) {
        g_pendingNames[key] = PendingName{name, ::GetTickCount64() + 60000};
        return;
    }
    g_pendingNames.erase(key);
    ++g_applied;
    UE_LOGI("camera_screen_sync: CLIENT camera '%ls' named the host's '%ls'", key.c_str(), name.c_str());
}

void OnTvAsk(int senderSlot, const N::TvCameraPayload& p) {
    if (!IsHost() || senderSlot <= 0 || senderSlot >= static_cast<int>(N::kMaxPeers)) return;
    void* const tv = TvOf(p.tvEid);
    const std::wstring camKey = N::StringFromWireKey(p.camKey);
    if (!tv || !BindTv(tv, camKey)) {
        UE_LOGW("camera_screen_sync: HOST slot %d's TV eid %u to camera '%ls' not bound", senderSlot, p.tvEid,
                camKey.c_str());
        return;
    }
    g_tvs[p.tvEid] = camKey;
    ++g_applied;
    Send(N::ReliableKind::TvCameraState, &p, sizeof(p), senderSlot);
    UE_LOGI("camera_screen_sync: HOST slot %d's TV eid %u shows camera '%ls'", senderSlot, p.tvEid, camKey.c_str());
}

void OnTvState(const N::TvCameraPayload& p) {
    if (!IsClient()) return;
    void* const tv = TvOf(p.tvEid);
    const std::wstring camKey = N::StringFromWireKey(p.camKey);
    const bool ok = tv && BindTv(tv, camKey);
    if (ok) ++g_applied;
    UE_LOGI("camera_screen_sync: CLIENT TV eid %u to the host's camera '%ls' (bound=%d)", p.tvEid, camKey.c_str(),
            ok ? 1 : 0);
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    // Every camera's name, not only those renamed: an unnamed camera takes CAM_<n> by its place in each machine's own
    // list of cameras, so the same camera is numbered apart on each peer, and a TV is found by that name.
    std::unordered_map<std::wstring, std::wstring> all = g_names;
    for (const wchar_t* cls : {L"prop_camera_bad_C", L"prop_camera_C", L"prop_camera_good_C", L"prop_camera_ultra_C",
                               L"prop_camera_s_C", L"prop_camera_cursed_C"}) {
        static void* found[128];
        const int n = ue_wrap::world_instances::Find(cls, found, 128);
        for (int i = 0; i < n; ++i) {
            const std::wstring key = coop::interactable_sync::CameraKeyOf(found[i]);
            std::wstring name;
            if (!key.empty() && !all.count(key) && ReadName(found[i], name) && !name.empty()) all[key] = name;
        }
    }
    for (const auto& kv : all) {
        const N::CameraNamePayload p = NamePayload(kv.first, kv.second);
        if (g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::CameraNameState, &p, sizeof(p)))
            ++g_states;
    }
    for (const auto& kv : g_tvs) {
        const N::TvCameraPayload p = TvPayload(kv.first, kv.second);
        if (g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::TvCameraState, &p, sizeof(p)))
            ++g_states;
    }
}

void OnDisconnect() {
    if (g_asks || g_states || g_applied)
        UE_LOGI("camera_screen_sync: session end -- client asks %u, host states %u, applied %u", g_asks, g_states,
                g_applied);
    g_names.clear();
    g_tvs.clear();
    g_pendingNames.clear();
    g_asks = g_states = g_applied = 0;
}

}  // namespace coop::camera_screen_sync
