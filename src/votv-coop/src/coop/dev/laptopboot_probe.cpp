// coop/dev/laptopboot_probe.cpp -- [dev] a joiner's laptop boot with a disc in, counted on every peer. See the header.

#include "coop/dev/laptopboot_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/devices/laptop.h"

#include <windows.h>

#include <cstdint>

namespace coop::dev::laptopboot_probe {
namespace {

namespace L = ue_wrap::laptop;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_LAPTOPBOOT_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;  // slot 1: 0 none, 1 first press done, 2 off done, 3 on again done

void Press(const char* why) {
    L::PowerState ps{};
    L::ReadPower(ps);
    const bool ok = L::CallPowerToggle();
    UE_LOGI("[LAPBOOT] slot 1 presses the laptop's power (%s): before powered=%d opened=%d anim=%d, ok=%d", why,
            ps.powered ? 1 : 0, ps.isOpened ? 1 : 0, ps.anim ? 1 : 0, ok ? 1 : 0);
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    if (!L::EnsureResolved() || !L::Instance()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[LAPBOOT] probe armed slot=%d", slot);
    }
    const uint64_t t = Now() - g_startAt;
    if (!host && slot == 1) {
        L::PowerState ps{};
        L::ReadPower(ps);
        if (g_step == 0 && t >= 50000u) {
            g_step = 1;
            if (!ps.isOpened) Press("on");
            else UE_LOGI("[LAPBOOT] slot 1: the laptop is already on at 50 s, not pressed");
        } else if (g_step == 1 && t >= 70000u) {
            g_step = 2;
            Press("off");
        } else if (g_step == 2 && t >= 85000u) {
            g_step = 3;
            Press("on again");
        }
    }
    if (t < 20000u || t > 140000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 3000u;
    L::PowerState ps{};
    const bool pr = L::ReadPower(ps);
    int32_t fd = -1, fb = -1, uid = -1, rw = -1;
    const bool qr = L::ReadQuadInts(fd, fb, uid, rw);
    UE_LOGI("[LAPBOOT] t=%llus slot=%d power(read=%d) powered=%d opened=%d anim=%d | quad(read=%d) data=%d buffer=%d "
            "uids=%d readwrites=%d",
            static_cast<unsigned long long>(t / 1000), slot, pr ? 1 : 0, ps.powered ? 1 : 0, ps.isOpened ? 1 : 0,
            ps.anim ? 1 : 0, qr ? 1 : 0, fd, fb, uid, rw);
}

}  // namespace coop::dev::laptopboot_probe
