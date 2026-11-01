// harness/autotest/autotest_noclip.cpp -- TEST-ONLY: a player's camera crosses the world's static geometry within a frame.
//
// mainPlayer_C's tick traces from last frame's camera to this one, and a hit is the game's way into the backrooms. One peer,
// VOTVCOOP_NOCLIP_WHO (host or client), is moved through the floor and both report what became of the world and the session.
// Gated by VOTVCOOP_RUN_NOCLIP; the folder is VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>

namespace harness::autotest {
namespace {

namespace E = ue_wrap::engine;
namespace R = ue_wrap::reflection;

std::string g_dir;
int g_fails = 0;

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Mark(const char* name) {
    std::string tmp = g_dir + "\\" + name + ".tmp";
    { std::ofstream(tmp, std::ios::binary | std::ios::trunc) << "x"; }
    ::MoveFileExA(tmp.c_str(), (g_dir + "\\" + name).c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool Await(const char* name, int seconds) {
    for (int i = 0; i < seconds * 10; ++i) {
        if (std::ifstream(g_dir + "\\" + name)) return true;
        ::Sleep(100);
    }
    UE_LOGW("noclip: VERDICT ABORT -- no '%hs' from the other peer", name);
    return false;
}

void Check(bool ok, const char* what) {
    UE_LOGI("noclip: %hs %hs", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fails;
}

bool BackroomsEnabled() {
    void* gm = ue_wrap::world_singleton::Gamemode();
    int32_t off = -1;
    uint8_t mask = 0;
    if (!gm || !R::FindBoolProperty(R::ClassOf(gm), L"backroomsEnabled", off, mask)) return false;
    return (static_cast<uint8_t*>(gm)[off] & mask) != 0;
}

void WaitInWorld() {
    void* pawn = nullptr;
    bool ready = !IsClientRole();
    for (int i = 0; i < 480 && (!pawn || !ready); ++i) {
        OnGameThread([&] {
            pawn = coop::players::Registry::Get().Local();
            if (IsClientRole()) ready = coop::net_pump::HasAnnouncedWorldReady();
        });
        if (!pawn || !ready) ::Sleep(500);
    }
}

void RunClipper() {
    if (!Await("other_ready", 300)) return;
    ::Sleep(5000);
    void* world = nullptr;
    ue_wrap::FVector start{};
    bool enabled = false;
    OnGameThread([&] {
        world = ue_wrap::world_identity::CurrentWorld();
        enabled = BackroomsEnabled();
        void* pawn = coop::players::Registry::Get().Local();
        if (!pawn || !E::TryGetActorLocation(pawn, start)) return;
        Check(!enabled, "the way into the backrooms is off in this session");
        UE_LOGI("noclip: clipping from (%.0f,%.0f,%.0f), backroomsEnabled=%d", start.X, start.Y, start.Z, enabled ? 1 : 0);
        E::TeleportTo(pawn, {start.X, start.Y, start.Z - 1500.f}, {});
    });
    Mark("clipped");
    bool sameWorld = true;
    for (int t = 0; t < 30; ++t) {
        ::Sleep(2000);
        OnGameThread([&] {
            sameWorld = sameWorld && ue_wrap::world_identity::CurrentWorld() == world;
            ue_wrap::FVector at{};
            void* pawn = coop::players::Registry::Get().Local();
            if (pawn) E::TryGetActorLocation(pawn, at);
            UE_LOGI("noclip: t=%d at=(%.0f,%.0f,%.0f) sameWorld=%d", t * 2, at.X, at.Y, at.Z, sameWorld ? 1 : 0);
        });
    }
    Check(sameWorld, "the clipper stayed in its world");
    Mark("clipper_done");
    UE_LOGI("noclip: DONE on the clipper fails=%d", g_fails);
}

void RunWatcher() {
    void* world = nullptr;
    OnGameThread([&] { world = ue_wrap::world_identity::CurrentWorld(); });
    Mark("other_ready");
    if (!Await("clipped", 300)) return;
    Await("clipper_done", 90);
    bool sameWorld = false;
    OnGameThread([&] { sameWorld = ue_wrap::world_identity::CurrentWorld() == world; });
    Check(sameWorld, "the other peer stayed in its world");
    bool enabled = true;
    OnGameThread([&] { enabled = BackroomsEnabled(); });
    Check(!enabled, "the way into the backrooms is off here too");
    UE_LOGI("noclip: DONE on the watcher fails=%d", g_fails);
}

}  // namespace

DWORD WINAPI NoclipThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("noclip: VERDICT ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    WaitInWorld();
    if (IsClientRole()) {
        // Both peers must be in: the client is the later, so it speaks first when it is the watcher, and waits on the host's word otherwise.
        ::Sleep(5000);
        if (coop::config::ReadEnv("VOTVCOOP_NOCLIP_WHO") == "client") {
            Mark("other_ready");
            RunClipper();
        } else {
            RunWatcher();
        }
    } else if (coop::config::ReadEnv("VOTVCOOP_NOCLIP_WHO") == "client") {
        RunWatcher();
    } else {
        RunClipper();
    }
    return 0;
}

}  // namespace harness::autotest
