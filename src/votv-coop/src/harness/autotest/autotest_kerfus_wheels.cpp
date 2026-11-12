// harness/autotest/autotest_kerfus_wheels.cpp -- TEST-ONLY: a moving Kerfus's parts, as each of two peers sees them.
//
// The host stands by the world's Kerfus (p_kerfus_C), fills its energy and turns it on with its own press, so it
// follows the host, then walks away from it in 4 m steps. Each second for 60 s each peer logs `kerfus-wheels: STATE`:
// the Kerfus's root place and each of its own primitive components, by name, with its distance from the root and
// whether it simulates physics; a component that wanders off the root is a part come loose. Gated by
// VOTVCOOP_RUN_KERFUS_WHEELS; the folder is VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "coop/session/net_pump.h"
#include "harness/screenshot.h"
#include "ue_wrap/actors/kerfus.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/engine/engine_physics.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <string>

namespace harness::autotest {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
namespace K = ue_wrap::kerfus;

std::string g_dir;
void* g_k = nullptr;   // game thread only

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

const char* Who() { return IsClientRole() ? "client" : "host"; }
void* Me() { return coop::players::Registry::Get().Local(); }

void Write(const std::string& name) { std::ofstream(g_dir + "\\" + name, std::ios::binary | std::ios::trunc) << "x"; }

bool Await(const std::string& name, int seconds = 300) {
    for (int i = 0; i < seconds * 10; ++i) {
        if (std::ifstream(g_dir + "\\" + name)) return true;
        ::Sleep(100);
    }
    UE_LOGW("kerfus-wheels: ABORT -- no '%hs' from the other peer", name.c_str());
    return false;
}

// The player to `at` through its own teleportWObackrooms.
void PlaceMe(const ue_wrap::FVector& at) {
    void* f = R::FindFunction(R::ClassOf(Me()), L"teleportWObackrooms");
    ue_wrap::ParamFrame pf(f);
    ue_wrap::FTransform t{};
    t.TX = at.X; t.TY = at.Y; t.TZ = at.Z;
    if (f && pf.valid() && pf.Set<ue_wrap::FTransform>(L"newTransform", t)) ue_wrap::Call(Me(), pf);
}

void LogState(int t) {
    OnGameThread([t] {
        if (!g_k || !R::IsLive(g_k)) { UE_LOGI("kerfus-wheels: STATE %hs t=%d no kerfus", Who(), t); return; }
        ue_wrap::FVector root{};
        E::TryGetActorLocation(g_k, root);
        bool on = false;
        K::ReadActive(g_k, on);
        void* comps[16] = {};
        const int n = E::OwnPrimitiveComponents(g_k, comps, 16);
        std::string parts;
        for (int i = 0; i < n; ++i) {
            ue_wrap::FVector at{};
            if (!E::TryGetComponentLocation(comps[i], at)) continue;
            const float dx = at.X - root.X, dy = at.Y - root.Y, dz = at.Z - root.Z;
            char buf[160];
            std::snprintf(buf, sizeof(buf), " %ls=%.0f%hs", R::ToString(R::NameOf(comps[i])).c_str(),
                          std::sqrt(dx * dx + dy * dy + dz * dz), E::IsComponentSimulatingPhysics(comps[i]) ? "(sim)" : "");
            parts += buf;
        }
        UE_LOGI("kerfus-wheels: STATE %hs t=%d on=%d root=(%.0f,%.0f,%.0f)%hs", Who(), t, on ? 1 : 0, root.X, root.Y,
                root.Z, parts.c_str());
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
    // A Kerfus of the host's own, spawned 3 m ahead of it when the world has none; the client finds the mirror.
    if (!IsClientRole()) {
        OnGameThread([] {
            g_k = K::FindLive();
            if (g_k) return;
            // The yard beside the world's ATV, open ground the Kerfus can drive on.
            void* atv = nullptr;
            if (ue_wrap::atv::EnsureResolved())
                ue_wrap::atv::ForEachAtv([](void* ctx, void* a, int32_t) {
                    if (!*static_cast<void**>(ctx)) *static_cast<void**>(ctx) = a;
                }, &atv);
            ue_wrap::FVector me{};
            if (atv && E::TryGetActorLocation(atv, me)) PlaceMe({me.X + 300.f, me.Y + 300.f, me.Z + 100.f});
            else E::TryGetActorLocation(Me(), me);
            me = {me.X + 300.f, me.Y + 300.f, me.Z + 100.f};
            void* cls = ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/p_kerfus.p_kerfus_C");
            // 2.5 m to the side of where the host stands, 60 cm up, as a placed prop drops onto the ground.
            void* k = cls ? E::SpawnActor(cls, {me.X - 300.f + 250.f, me.Y - 300.f, me.Z - 40.f}) : nullptr;
            UE_LOGI("kerfus-wheels: host spawned a Kerfus %p", k);
            g_k = k;
        });
        ::Sleep(4000);
        Write("spawned");
    } else {
        if (!Await("spawned")) return;
        ::Sleep(3000);
        OnGameThread([] { g_k = K::FindLive(); });
    }
    if (!g_k) { UE_LOGW("kerfus-wheels: ABORT -- %hs has no Kerfus", Who()); return; }
    if (!IsClientRole()) {
        OnGameThread([] {
            ue_wrap::FVector at{};
            E::TryGetActorLocation(g_k, at);
            PlaceMe({at.X + 250.f, at.Y, at.Z + 100.f});
        });
        ::Sleep(2000);
        OnGameThread([] {
            K::WriteEnergy(g_k, 100.f);
            bool on = false;
            K::ReadActive(g_k, on);
            if (!on) K::RunActionOptionIndex(g_k, Me(), 8);
            K::ReadActive(g_k, on);
            UE_LOGI("kerfus-wheels: host turned the Kerfus on (on=%d)", on ? 1 : 0);
        });
        Write("on");
    } else if (!Await("on")) {
        return;
    }
    for (int t = 0; t < 60; ++t) {
        if (!IsClientRole() && t > 0 && t % 6 == 0) {
            OnGameThread([] {
                ue_wrap::FVector me{};
                E::TryGetActorLocation(Me(), me);
                PlaceMe({me.X + 400.f, me.Y, me.Z + 50.f});
            });
        }
        LogState(t);
        ::Sleep(1000);
    }
    // Phase 2: the client turns it off and on with its own presses (asked of the host), so it follows the client,
    // and walks away from it in 4 m steps; each peer shoots the Kerfus from 3 m every 15 s.
    if (IsClientRole()) {
        OnGameThread([] {
            ue_wrap::FVector at{};
            E::TryGetActorLocation(g_k, at);
            PlaceMe({at.X + 200.f, at.Y, at.Z + 100.f});
        });
        ::Sleep(2000);
        for (int i = 0; i < 2; ++i) {
            OnGameThread([] { K::RunActionOptionIndex(g_k, Me(), 8); });
            ::Sleep(2500);
        }
        UE_LOGI("kerfus-wheels: client pressed the Kerfus off and on");
        Write("client_on");
    } else if (!Await("client_on")) {
        return;
    }
    for (int t = 60; t < 120; ++t) {
        if (IsClientRole() && t % 6 == 0) {
            OnGameThread([] {
                ue_wrap::FVector me{};
                E::TryGetActorLocation(Me(), me);
                PlaceMe({me.X, me.Y - 400.f, me.Z + 50.f});
            });
        }
        if (t % 15 == 5) {
            OnGameThread([] {   // look at the Kerfus from 3 m, beside it
                ue_wrap::FVector at{};
                if (!E::TryGetActorLocation(g_k, at)) return;
                if (!IsClientRole()) PlaceMe({at.X + 300.f, at.Y, at.Z + 100.f});
                const ue_wrap::FVector cam = E::GetCameraLocation();
                const float dx = at.X - cam.X, dy = at.Y - cam.Y, dz = at.Z - cam.Z;
                if (void* c = E::GetController(Me()))
                    E::SetControlRotation(c, ue_wrap::FRotator{std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 57.2957795f,
                                                               std::atan2(dy, dx) * 57.2957795f, 0.f});
            });
            ::Sleep(600);
            wchar_t label[48];
            std::swprintf(label, 48, L"kerfus-%hs-t%d", Who(), t);
            harness::screenshot::Capture(label);
        }
        LogState(t);
        ::Sleep(1000);
    }
    UE_LOGI("kerfus-wheels: DONE %hs", Who());
}

}  // namespace

DWORD WINAPI KerfusWheelsThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("kerfus-wheels: ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    Run();
    return 0;
}

}  // namespace harness::autotest
