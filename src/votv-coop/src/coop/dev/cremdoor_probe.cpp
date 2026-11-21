// coop/dev/cremdoor_probe.cpp -- [dev] a far client works the cremator's door, for the frame rate. See the header.

#include "coop/dev/cremdoor_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/devices/cremator.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace coop::dev::cremdoor_probe {
namespace {

namespace C = ue_wrap::cremator;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_CREMDOOR_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0;
int g_moves = 0;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    if (session->role() == coop::net::Role::Host || coop::players::Registry::Get().LocalPeerId() != 1) return;
    if (!g_startAt) { g_startAt = Now(); UE_LOGI("[CREMDOOR] probe armed"); }
    const uint64_t t = Now() - g_startAt;
    if (g_moves >= 6 || t < 60000u + 8000u * static_cast<uint64_t>(g_moves)) return;
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector at{};
    if (!me || !ue_wrap::engine::TryGetActorLocation(me, at) || !C::EnsureResolved()) return;
    const C::Nearest n = C::FindNearestFirebox(at);
    if (!n.actor) { UE_LOGW("[CREMDOOR] no cremator"); g_moves = 6; return; }
    bool was = false;
    const bool open = (g_moves % 2) == 0;
    const bool ok = open ? C::OpenDoor(n.actor, was) : C::ShutDoor(n.actor, was);
    ++g_moves;
    UE_LOGI("[CREMDOOR] slot 1 %s the cremator's door (ok=%d, was %d) from %.0f cm away", open ? "opens" : "shuts",
            ok ? 1 : 0, was ? 1 : 0, std::hypot(n.at.X - at.X, n.at.Y - at.Y, n.at.Z - at.Z));
}

}  // namespace coop::dev::cremdoor_probe
