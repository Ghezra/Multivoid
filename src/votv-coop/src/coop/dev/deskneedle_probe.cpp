// coop/dev/deskneedle_probe.cpp -- [dev] the desk's needle and save latch, logged. See the header.

#include "coop/dev/deskneedle_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/desk/console_desk.h"
#include "ue_wrap/desk/desk_detector.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace coop::dev::deskneedle_probe {
namespace {

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_DESKNEEDLE_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0, g_next = 0;
float g_lastNeedle = -2.f;
int g_lastCan = -1;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    if (!g_startAt) g_startAt = Now();
    const uint64_t t = Now() - g_startAt;
    if (t < 20000u || t > 240000u || Now() < g_next) return;
    g_next = Now() + 1000u;
    void* const desk = ue_wrap::console_desk::Instance();
    if (!desk) return;
    float needle = -1.f;
    bool can = false;
    const bool rn = ue_wrap::desk_detector::ReadNeedle(desk, needle);
    const bool rc = ue_wrap::desk_detector::ReadCanDL(desk, can);
    if (rn && rc && std::fabs(needle - g_lastNeedle) < 0.0005f && (can ? 1 : 0) == g_lastCan) return;
    g_lastNeedle = needle;
    g_lastCan = can ? 1 : 0;
    UE_LOGI("[NEEDLE] t=%llus slot=%d needle=%.4f (read=%d) canDL=%d (read=%d)", static_cast<unsigned long long>(t / 1000),
            coop::players::Registry::Get().LocalPeerId(), needle, rn ? 1 : 0, can ? 1 : 0, rc ? 1 : 0);
}

}  // namespace coop::dev::deskneedle_probe
