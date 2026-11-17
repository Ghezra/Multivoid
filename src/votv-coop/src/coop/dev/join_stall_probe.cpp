// coop/dev/join_stall_probe.cpp -- [dev] a client's game thread held still while it joins (VOTVCOOP_JOIN_STALL_MS=<ms>
// on the client, VOTVCOOP_JOIN_STALL_AT_MS=<ms> after its world first reaches gameplay, 2000 by default). A player's join
// ended in "No answer from the host" after one frame of 22.7 s, the spawners already ticking: this holds one frame that
// long, once, so the session's own lines say what the stall does to the connection and the join.

#include "coop/dev/join_stall_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/world_identity.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <string>

namespace coop::dev::join_stall_probe {
namespace {

long EnvMs(const char* key, long fallback) {
    const std::string v = coop::config::ReadEnv(key);
    return v.empty() ? fallback : std::atol(v.c_str());
}

uint64_t g_gameplayAt = 0;
bool g_done = false;

}  // namespace

void Tick(coop::net::Session* session) {
    static const long kStallMs = EnvMs("VOTVCOOP_JOIN_STALL_MS", 0);
    if (kStallMs <= 0 || g_done || !session || !session->running() || session->role() != coop::net::Role::Client) return;
    if (ue_wrap::world_identity::CurrentWorldKind() != ue_wrap::world_identity::WorldKind::Gameplay) return;
    if (!g_gameplayAt) g_gameplayAt = ::GetTickCount64();
    static const long kAtMs = EnvMs("VOTVCOOP_JOIN_STALL_AT_MS", 2000);
    if (::GetTickCount64() - g_gameplayAt < static_cast<uint64_t>(kAtMs)) return;
    g_done = true;
    UE_LOGI("[JOINSTALL] holding the game thread %ld ms (connected=%d)", kStallMs, session->connected() ? 1 : 0);
    ue_wrap::log::Flush();
    ::Sleep(static_cast<DWORD>(kStallMs));
    UE_LOGI("[JOINSTALL] the game thread runs again (connected=%d)", session->connected() ? 1 : 0);
}

}  // namespace coop::dev::join_stall_probe
