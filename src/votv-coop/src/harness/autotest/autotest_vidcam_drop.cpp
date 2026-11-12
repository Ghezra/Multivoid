// harness/autotest/autotest_vidcam_drop.cpp -- TEST-ONLY: a video camera picked up, recording, and dropped, as each
// peer's camera manager (RCM_cameraManager_C) sees it. Ghezra/Multivoid issue 23.
//
// The host spawns a low quality video camera (prop_vidcam_low_C) beside itself. The doer (the client, or the host
// with VOTVCOOP_VIDCAM_DOER=host) stands by it, takes it in hand with the player's own Hold Object, starts a recording
// with the camera's own LMB use when VOTVCOOP_VIDCAM_RECORD=1, and drops it with the player's own forceDrop. Each
// second each peer logs `vidcam: STATE`: the held actor, its manager's camProp, the recording and pause bits, the
// manager's isInContainer, the overlay widget's visibility, and the gamemode's active events. Gated by
// VOTVCOOP_RUN_VIDCAM_DROP; the folder is VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/active_events.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace harness::autotest {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
namespace AE = ue_wrap::active_events;

std::string g_dir;
bool g_hostDoes = false;
bool g_record = false;
bool g_stage = false;  // VOTVCOOP_VIDCAM_STAGE=1: place the doer facing the camera, then stop
bool g_skip = false;  // VOTVCOOP_VIDCAM_SKIP=1: run the clock to the limit after the drop
int g_tailS = 20;   // VOTVCOOP_VIDCAM_TAIL_S: how long the drop is watched

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

const char* Who() { return IsClientRole() ? "client" : "host"; }
bool IDo() { return IsClientRole() != g_hostDoes; }
void* Me() { return coop::players::Registry::Get().Local(); }

void Write(const std::string& name) { std::ofstream(g_dir + "\\" + name, std::ios::binary | std::ios::trunc) << "x"; }

bool Await(const std::string& name, int seconds = 300) {
    for (int i = 0; i < seconds * 10; ++i) {
        if (std::ifstream(g_dir + "\\" + name)) return true;
        ::Sleep(100);
    }
    UE_LOGW("vidcam: ABORT -- no '%hs' from the other peer", name.c_str());
    return false;
}

void PlaceMe(const ue_wrap::FVector& at) {
    void* f = R::FindFunction(R::ClassOf(Me()), L"teleportWObackrooms");
    ue_wrap::ParamFrame pf(f);
    ue_wrap::FTransform t{};
    t.TX = at.X; t.TY = at.Y; t.TZ = at.Z;
    if (f && pf.valid() && pf.Set<ue_wrap::FTransform>(L"newTransform", t)) ue_wrap::Call(Me(), pf);
}

template <class T>
T Field(void* obj, const wchar_t* name) {
    T v{};
    if (!obj) return v;
    const int32_t off = R::FindPropertyOffset(R::ClassOf(obj), name);
    if (off >= 0) v = *reinterpret_cast<T*>(static_cast<char*>(obj) + off);
    return v;
}

// A no-argument function's bool ReturnValue, found by dispatch (the native BaseRecord declares getIsRecording).
int CallBool(void* obj, const wchar_t* fn) {
    void* f = obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), fn) : nullptr;
    if (!f) return -1;
    ue_wrap::ParamFrame pf(f);
    if (!pf.valid() || !ue_wrap::Call(obj, pf)) return -1;
    return pf.Get<bool>(L"ReturnValue") ? 1 : 0;
}

void* FirstOf(const wchar_t* className, int* count = nullptr) {
    void* cls = OI::ClassByName(className);
    struct Ctx { void* first; int n; } c{nullptr, 0};
    if (cls)
        OI::ForEachInstance(cls, [](void* ctx, void* o, int32_t) {
            auto* k = static_cast<Ctx*>(ctx);
            if (!R::IsLive(o) || R::ToString(R::NameOf(o)).rfind(L"Default__", 0) == 0) return;
            if (!k->first) k->first = o;
            ++k->n;
        }, &c);
    if (count) *count = c.n;
    return c.first;
}

std::string Name(void* o) {
    if (!o) return "none";
    if (!R::IsLive(o)) return "dead";
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%ls(%ls)", R::ToString(R::NameOf(o)).c_str(), R::ClassNameOf(o).c_str());
    return buf;
}

void LogState(const char* tag) {
    OnGameThread([tag] {
        int cams = 0;
        FirstOf(L"prop_vidcam_low_C", &cams);
        void* m = FirstOf(L"RCM_cameraManager_C");
        void* held = Field<void*>(Me(), L"holding_actor");
        void* camProp = Field<void*>(m, L"camProp");
        void* camUI = Field<void*>(m, L"camUI");
        const int vis = camUI ? static_cast<int>(Field<uint8_t>(camUI, L"Visibility")) : -1;
        int32_t n = -1;
        AE::ReadCount(n);
        std::string senders;
        AE::Senders s{};
        if (AE::ReadSenders(s))
            for (int32_t i = 0; i < s.num; ++i) senders += " " + Name(s.data[i]);
        UE_LOGI("vidcam: STATE %hs %hs held=%hs camProp=%hs rec=%d paused=%d inContainer=%d canAccess=%d uiVis=%d "
                "vidcams=%d time=%.1f/%.0f battery=%.2f events=%d [%hs ]",
                Who(), tag, Name(held).c_str(), Name(camProp).c_str(), CallBool(m, L"getIsRecording"),
                CallBool(m, L"getIsPaused"), Field<bool>(m, L"isInContainer") ? 1 : 0,
                Field<bool>(m, L"canAccess") ? 1 : 0, vis, cams, Field<float>(m, L"time"), Field<float>(m, L"maxTime"),
                Field<float>(m, L"batteryPower"), n, senders.c_str());
    });
}

void Run() {
    if (IsClientRole()) {
        bool ready = false;
        for (int i = 0; i < 480 && !ready; ++i) {
            OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
            if (!ready) ::Sleep(500);
        }
        ::Sleep(10000);
        Write("client_ready");
    } else {
        ::Sleep(20000);
        if (!Await("client_ready")) return;
    }
    LogState("start");
    if (!IsClientRole()) {
        OnGameThread([] {
            void* cls = OI::ClassByName(L"prop_vidcam_low_C");
            if (!cls) cls = ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/prop_vidcam_low.prop_vidcam_low_C");
            ue_wrap::FVector at{};
            E::TryGetActorLocation(Me(), at);
            void* cam = cls ? E::SpawnActor(cls, {at.X + 150.f, at.Y, at.Z}) : nullptr;
            UE_LOGI("vidcam: host spawned %hs (class %p)", Name(cam).c_str(), cls);
        });
        ::Sleep(4000);
        Write("spawned");
    } else if (!Await("spawned")) {
        return;
    }
    ::Sleep(3000);
    LogState("spawned");
    if (IDo()) {
        OnGameThread([] {
            void* cam = FirstOf(L"prop_vidcam_low_C");
            ue_wrap::FVector at{};
            if (!cam || !E::TryGetActorLocation(cam, at)) { UE_LOGW("vidcam: %hs finds no camera", Who()); return; }
            PlaceMe({at.X - 120.f, at.Y, at.Z + 60.f});
        });
        ::Sleep(1500);
        if (g_stage) {   // VOTVCOOP_VIDCAM_STAGE=1: face the camera on the floor and leave the rest to the player
            OnGameThread([] {
                if (void* c = E::GetController(Me())) E::SetControlRotation(c, ue_wrap::FRotator{-35.f, 0.f, 0.f});
            });
            UE_LOGI("vidcam: STAGED %hs -- the camera is ahead of this player", Who());
            return;
        }
        OnGameThread([] {
            void* cam = FirstOf(L"prop_vidcam_low_C");
            bool collected = false;
            const bool ok = cam && E::CallMainPlayerHoldObject(Me(), cam, collected);
            UE_LOGI("vidcam: %hs Hold Object ran=%d collected=%d", Who(), ok ? 1 : 0, collected ? 1 : 0);
        });
    }
    for (int t = 0; t < 4; ++t) { ::Sleep(1000); LogState("held"); }
    if (IDo() && g_record) {
        OnGameThread([] {
            void* held = Field<void*>(Me(), L"holding_actor");
            void* f = held ? R::FindDispatchFunctionCached(R::ClassOf(held), L"playerHandUse_LMB") : nullptr;
            ue_wrap::ParamFrame pf(f);
            const bool ok = f && pf.valid() && pf.Set<void*>(L"player", Me()) && ue_wrap::Call(held, pf);
            UE_LOGI("vidcam: %hs pressed record on %hs ran=%d", Who(), Name(held).c_str(), ok ? 1 : 0);
        });
    }
    for (int t = 0; t < 4; ++t) { ::Sleep(1000); LogState("recording"); }
    if (IDo()) {
        OnGameThread([] {
            void* f = R::FindDispatchFunctionCached(R::ClassOf(Me()), L"forceDrop");
            ue_wrap::ParamFrame pf(f);
            const bool ok = f && pf.valid() && ue_wrap::Call(Me(), pf);
            UE_LOGI("vidcam: %hs forceDrop ran=%d", Who(), ok ? 1 : 0);
        });
    }
    for (int t = 0; t < g_tailS; ++t) {
        ::Sleep(1000);
        if (t == 5 && g_skip && IDo())   // [dev write] the manager's clock to 5 s before its own limit
            OnGameThread([] {
                void* m = FirstOf(L"RCM_cameraManager_C");
                const int32_t off = m ? R::FindPropertyOffset(R::ClassOf(m), L"time") : -1;
                if (off >= 0) *reinterpret_cast<float*>(static_cast<char*>(m) + off) = Field<float>(m, L"maxTime") - 5.f;
                UE_LOGI("vidcam: %hs moved the recording clock to its limit less 5 s", Who());
            });
        if (t < 20 || t % 10 == 0) LogState("dropped");
    }
    UE_LOGI("vidcam: DONE %hs", Who());
}

}  // namespace

DWORD WINAPI VidcamDropThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("vidcam: ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    g_hostDoes = coop::config::ReadEnv("VOTVCOOP_VIDCAM_DOER") == "host";
    g_record = coop::config::ReadEnv("VOTVCOOP_VIDCAM_RECORD") == "1";
    g_skip = coop::config::ReadEnv("VOTVCOOP_VIDCAM_SKIP") == "1";
    g_stage = coop::config::ReadEnv("VOTVCOOP_VIDCAM_STAGE") == "1";
    const std::string tail = coop::config::ReadEnv("VOTVCOOP_VIDCAM_TAIL_S");
    if (!tail.empty()) g_tailS = std::atoi(tail.c_str());
    Run();
    return 0;
}

}  // namespace harness::autotest
