// coop/dev/dial_probe.cpp -- [dev] the signal desk's frequency dial turned by a client (VOTVCOOP_DIAL_PROBE=1).
//
// The desk (analogDScreenTest_C) integrates DL_FrFilterOffset by DL_FrFilterSpeed each tick while DL_activeFrFilter is
// on (calculate_download), and a scroll on the dial adds to the speed (addFilterParam). CLIENT, 20 s after it is
// connected: switches the filter on; at 24 s sets the speed to VOTVCOOP_DIAL_SPEED (default 10), as a scroll does; at
// 26 s sets it back to 0; at 30 s a nudge: the speed up for 150 ms and back to 0. BOTH, every 20 ms from 22 s to 60 s (the host connected first):
// the desk's speed and offset (`[DIAL]`), so the client's own view of the offset can be laid beside the host's.

#include "coop/dev/dial_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/desk/console_desk.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>

namespace coop::dev::dial_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace CD = ue_wrap::console_desk;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_DIAL_PROBE") == "1";
    return on;
}

float Speed() {
    static const float s = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_DIAL_SPEED");
        return v.empty() ? 10.f : static_cast<float>(std::atof(v.c_str()));
    }();
    return s;
}

float* FloatField(const wchar_t* name) {
    void* const desk = CD::Instance();
    const int32_t off = desk ? R::FindPropertyOffset(R::ClassOf(desk), name) : -1;
    return off >= 0 ? reinterpret_cast<float*>(static_cast<char*>(desk) + off) : nullptr;
}

bool SetBool(const wchar_t* name, bool v) {
    void* const desk = CD::Instance();
    int32_t off = -1;
    uint8_t mask = 0;
    if (!desk || !R::FindBoolProperty(R::ClassOf(desk), name, off, mask)) return false;
    uint8_t& b = static_cast<uint8_t*>(desk)[off];
    b = v ? static_cast<uint8_t>(b | mask) : static_cast<uint8_t>(b & ~mask);
    return true;
}

void SetSpeed(float v, const char* why) {
    float* const speed = FloatField(L"DL_FrFilterSpeed");
    if (speed) *speed = v;
    FILETIME ft{};
    ::GetSystemTimePreciseAsFileTime(&ft);
    const uint64_t wallMs = ((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000ull;
    UE_LOGI("[DIAL] client: %s -- the speed set to %.1f at wall=%llu (ok=%d)", why, v,
            static_cast<unsigned long long>(wallMs % 10000000ull), speed ? 1 : 0);
}

uint64_t g_startAt = 0, g_reportAt = 0, g_nudgeEndAt = 0;
int g_step = 0;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    if (!CD::EnsureResolved()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[DIAL] probe armed role=%s", host ? "host" : "client");
    }
    const uint64_t t = Now() - g_startAt;
    if (!host) {
        if (g_step == 0 && t >= 20000u) {
            g_step = 1;
            UE_LOGI("[DIAL] client: the frequency filter switched on (ok=%d)", SetBool(L"DL_activeFrFilter", true) ? 1 : 0);
        }
        if (g_step == 1 && t >= 24000u) { g_step = 2; SetSpeed(Speed(), "a scroll up"); }
        if (g_step == 2 && t >= 26000u) { g_step = 3; SetSpeed(0.f, "a scroll back to 0"); }
        if (g_step == 3 && t >= 30000u) { g_step = 4; g_nudgeEndAt = Now() + 150; SetSpeed(Speed(), "a nudge up"); }
        if (g_step == 4 && g_nudgeEndAt && Now() >= g_nudgeEndAt) { g_step = 5; SetSpeed(0.f, "the nudge back to 0"); }
    }
    if (t < 22000u || t > 60000u || Now() < g_reportAt) return;
    g_reportAt = Now() + 20;
    const float* const speed = FloatField(L"DL_FrFilterSpeed");
    const float* const offset = FloatField(L"DL_FrFilterOffset");
    // The wall clock in milliseconds: both rig peers share the machine's, so their lines lay side by side.
    FILETIME ft{};
    ::GetSystemTimePreciseAsFileTime(&ft);
    const uint64_t wallMs = ((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000ull;
    UE_LOGI("[DIAL] wall=%llu speed=%.2f offset=%.3f", static_cast<unsigned long long>(wallMs % 10000000ull),
            speed ? *speed : -1.f, offset ? *offset : -1.f);
}

}  // namespace coop::dev::dial_probe
