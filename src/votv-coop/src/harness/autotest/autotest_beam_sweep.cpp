// harness/autotest/autotest_beam_sweep.cpp -- TEST-ONLY: a remote player's flashlight beam, measured frame by
// frame against the view that aims it.
//
// The CLIENT is the source: once its join settles it switches its flashlight on and sweeps its view in two sines
// (yaw +-40 deg over 4 s, pitch +-10 deg over 3 s), smooth by construction, writing its own camera rotation each
// tick. The HOST is the observer: it writes, each tick, the slot's puppet's streamed aim after interpolation
// (RemotePlayer::GetSyncedAimRotation), the cone's arm (lag_fl) and the cone (light_R) world rotations, and the
// puppet's actor yaw. Comparing the steps of each column says where a jitter enters: the stream and its
// interpolation, or the components the cone hangs from. Each row carries the machine's performance counter in
// seconds, one clock for both processes on one machine, and the world's own clock, which moves once a frame,
// so rows of one frame can be told apart. Gated by VOTVCOOP_RUN_BEAM_SWEEP; the CSV goes to VOTVCOOP_BEAM_CSV
// (one file per process).

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/player/item_activate.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/sdk_profile.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace harness::autotest {
namespace {

namespace GT = ue_wrap::game_thread;
namespace E  = ue_wrap::engine;
namespace P  = ue_wrap::profile;

constexpr int   kSweepS    = 20;
constexpr int   kObserveS  = 60;
constexpr float kYawAmpDefaultDeg = 40.f, kYawPeriodS = 4.f;   // VOTVCOOP_BEAM_YAW_AMP overrides the amplitude
constexpr float kPitchAmpDeg = 10.f, kPitchPeriodS = 3.f;
constexpr float kTwoPi = 6.28318530718f;

double NowS() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    ::QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(freq.QuadPart);
}

template <class Fn>
void OnGameThread(Fn&& fn) {
    GT::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void* ArmOf(void* mainPlayer) {
    void* arm = nullptr;
    std::memcpy(&arm, static_cast<uint8_t*>(mainPlayer) + P::off::AmainPlayer_lag_fl, sizeof(arm));
    return arm;
}

void RunSource(FILE* csv) {
    // The join settles first: the host's puppet of this player must exist before the sweep is worth watching.
    bool ready = false;
    for (int i = 0; i < 480 && !ready; ++i) {
        OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
        if (!ready) ::Sleep(500);
    }
    ::Sleep(10000);
    void* local = nullptr;
    OnGameThread([&] { local = coop::players::Registry::Get().Local(); });
    if (!local) { UE_LOGW("beam_sweep: SOURCE no local player -- abort"); return; }
    const bool on = coop::item_activate::DebugForceToggle(local);
    const std::string ampEnv = coop::config::ReadEnv("VOTVCOOP_BEAM_YAW_AMP");
    const float yawAmpDeg = ampEnv.empty() ? kYawAmpDefaultDeg : static_cast<float>(std::atof(ampEnv.c_str()));
    UE_LOGI("beam_sweep: SOURCE flashlight %s; sweeping yaw +-%.0f/%.0fs pitch +-%.0f/%.0fs for %d s", on ? "on" : "OFF",
            yawAmpDeg, kYawPeriodS, kPitchAmpDeg, kPitchPeriodS, kSweepS);
    float yaw0 = 0.f;
    OnGameThread([&] { yaw0 = E::GetControlRotation(E::GetController(local)).Yaw; });
    std::fprintf(csv, "t,wt,cam_pitch,cam_yaw,set_pitch,set_yaw\n");
    const double start = NowS();
    while (NowS() - start < kSweepS) {
        OnGameThread([&] {
            const double t = NowS() - start;
            const float yaw = yaw0 + yawAmpDeg * std::sin(kTwoPi * static_cast<float>(t) / kYawPeriodS);
            const float pitch = kPitchAmpDeg * std::sin(kTwoPi * static_cast<float>(t) / kPitchPeriodS);
            E::SetControlRotation(E::GetController(local), ue_wrap::FRotator{pitch, yaw, 0.f});
            const ue_wrap::FRotator cam = E::GetCameraRotation();
            float wt = -1.f;
            E::TryGetWorldTimeSeconds(wt);
            std::fprintf(csv, "%.5f,%.5f,%.4f,%.4f,%.4f,%.4f\n", NowS(), wt, cam.Pitch, cam.Yaw, pitch, yaw);
        });
        ::Sleep(1);
    }
    UE_LOGI("beam_sweep: SOURCE DONE");
}

void RunObserver(FILE* csv) {
    coop::RemotePlayer* rp = nullptr;
    for (int i = 0; i < 480 && !rp; ++i) {
        OnGameThread([&] {
            coop::RemotePlayer* p = coop::players::Registry::Get().Puppet(1);
            if (p && p->valid()) rp = p;
        });
        if (!rp) ::Sleep(500);
    }
    if (!rp) { UE_LOGW("beam_sweep: OBSERVER no puppet for slot 1 -- abort"); return; }
    UE_LOGI("beam_sweep: OBSERVER recording slot 1's beam for %d s", kObserveS);
    std::fprintf(csv, "t,wt,aim_pitch,aim_yaw,arm_pitch,arm_yaw,light_pitch,light_yaw,body_yaw\n");
    const double start = NowS();
    while (NowS() - start < kObserveS) {
        OnGameThread([&] {
            coop::RemotePlayer* p = coop::players::Registry::Get().Puppet(1);
            void* actor = (p && p->valid()) ? p->GetActor() : nullptr;
            if (!actor) return;
            const ue_wrap::FRotator aim = p->GetSyncedAimRotation();
            void* arm = ArmOf(actor);
            void* light = E::GetMainPlayerLightR(actor);
            const ue_wrap::FRotator a = arm ? E::GetComponentWorldRotation(arm) : ue_wrap::FRotator{};
            const ue_wrap::FRotator l = light ? E::GetComponentWorldRotation(light) : ue_wrap::FRotator{};
            ue_wrap::FRotator body{};
            E::TryGetActorRotation(actor, body);
            float wt = -1.f;
            E::TryGetWorldTimeSeconds(wt);
            std::fprintf(csv, "%.5f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", NowS(), wt, aim.Pitch, aim.Yaw,
                         a.Pitch, a.Yaw, l.Pitch, l.Yaw, body.Yaw);
        });
        ::Sleep(1);
    }
    UE_LOGI("beam_sweep: OBSERVER DONE");
}

}  // namespace

DWORD WINAPI BeamSweepThread(LPVOID /*arg*/) {
    const std::string path = coop::config::ReadEnv("VOTVCOOP_BEAM_CSV");
    if (path.empty()) { UE_LOGW("beam_sweep: VOTVCOOP_BEAM_CSV is unset -- abort"); return 0; }
    FILE* csv = nullptr;
    if (fopen_s(&csv, path.c_str(), "w") != 0 || !csv) { UE_LOGW("beam_sweep: cannot write %s", path.c_str()); return 0; }
    if (IsClientRole()) RunSource(csv);
    else                RunObserver(csv);
    std::fclose(csv);
    return 0;
}

}  // namespace harness::autotest
