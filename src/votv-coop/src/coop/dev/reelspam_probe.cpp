// coop/dev/reelspam_probe.cpp -- [dev] a player spams the tape recorder's eject (VOTVCOOP_REELSPAM_PROBE=host|client
// names the ejecter; VOTVCOOP_REELSPAM_N presses, default 8, VOTVCOOP_REELSPAM_GAP ms apart, default 150).
//
// wallunit_tapes_C's actionOptionIndex, action 4 while the player looks at a tape, spawns that tape's reel with the
// slot's progress, empties the slot (-1) and puts the reel in the player's hand. HOST, 20 s after a client is
// connected: stops the machine and loads the big slot (reelBig 0.5, upd). THE EJECTER, from 25 s: the press, as the use key's handler makes
// it, with the unit's lookat_bigtape set, N times GAP ms apart. EVERY PEER, every second from 20 s to 45 s: the live
// big reels it holds and the slot (`[REELSPAM]`).

#include "coop/dev/reelspam_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/desk/tape_caddy.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace coop::dev::reelspam_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace OI = ue_wrap::object_index;
namespace TC = ue_wrap::tape_caddy;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Ejecter() {
    static const std::string f = coop::config::ReadEnv("VOTVCOOP_REELSPAM_PROBE");
    return f;
}

int EnvInt(const char* name, int dflt) {
    const std::string v = coop::config::ReadEnv(name);
    return v.empty() ? dflt : std::atoi(v.c_str());
}

int CountReels() {
    int n = 0;
    if (void* const cls = OI::ClassByName(L"prop_reel_big_C"))
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__")) ++*static_cast<int*>(ctx);
        }, &n);
    return n;
}

void SetBool(void* obj, const wchar_t* name, bool v) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return;
    uint8_t& b = static_cast<uint8_t*>(obj)[off];
    b = v ? static_cast<uint8_t>(b | mask) : static_cast<uint8_t>(b & ~mask);
}

// The press: actionOptionIndex(player, hit, action 4, lookAtComponent) on the unit, looking at the big tape.
bool Press(void* unit, void* me) {
    void* const fn = R::FindDispatchFunctionCached(R::ClassOf(unit), L"actionOptionIndex");
    if (!fn) return false;
    const int32_t pOff = R::FindParamOffset(fn, L"player"), aOff = R::FindParamOffset(fn, L"action");
    if (pOff < 0 || aOff < 0) return false;
    std::vector<uint8_t> frame(static_cast<size_t>(R::FunctionFrameSize(fn)), 0u);
    std::memcpy(frame.data() + pOff, &me, sizeof(me));
    frame[static_cast<size_t>(aOff)] = 4;
    SetBool(unit, L"lookat_usebox", false);
    SetBool(unit, L"lookat_smalltape", false);
    SetBool(unit, L"lookat_bigtape", true);
    return R::CallFunction(unit, fn, frame.data());
}

uint64_t g_startAt = 0, g_nextLog = 0, g_nextPress = 0;
int g_step = 0, g_pressed = 0;

}  // namespace

void Tick(coop::net::Session* session) {
    if (Ejecter().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const bool ejecter = (Ejecter() == "host") == host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[REELSPAM] probe armed role=%s ejecter=%d", host ? "host" : "client", ejecter ? 1 : 0);
    }
    const uint64_t t = Now() - g_startAt;
    if (!TC::EnsureResolved()) return;
    void* const unit = TC::Instance();
    void* const me = coop::players::Registry::Get().Local();
    if (host && g_step == 0 && t >= 20000u && unit) {
        g_step = 1;
        // Stopped: an eject is refused while the tapes are in motion (`active`).
        SetBool(unit, L"active", false);
        const bool ok = TC::WriteReel(TC::kReelBig, 0.5f) && TC::CallUpd();
        UE_LOGI("[REELSPAM] host: loaded the big slot, the machine stopped (ok=%d)", ok ? 1 : 0);
    }
    static const int kN = EnvInt("VOTVCOOP_REELSPAM_N", 8), kGap = EnvInt("VOTVCOOP_REELSPAM_GAP", 150);
    // Every copy stopped before the presses: the host's raw write of `active` is no change the appliance lane sends.
    if (unit && t >= 24000u && t < 25000u) SetBool(unit, L"active", false);
    if (ejecter && unit && me && t >= 25000u && g_pressed < kN && Now() >= g_nextPress) {
        g_nextPress = Now() + static_cast<uint64_t>(kGap);
        ++g_pressed;
        UE_LOGI("[REELSPAM] ejecter: press %d (ran=%d)", g_pressed, Press(unit, me) ? 1 : 0);
    }
    if (unit && t >= 20000u && t < 45000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 1000;
        float big = -2.f, little = -2.f;
        TC::ReadReels(big, little);
        int32_t aOff = -1;
        uint8_t aMask = 0;
        const int active = R::FindBoolProperty(R::ClassOf(unit), L"active", aOff, aMask)
                               ? ((static_cast<const uint8_t*>(unit)[aOff] & aMask) ? 1 : 0) : -1;
        UE_LOGI("[REELSPAM] t=%llu big reels %d, slot big %.2f, active %d", t / 1000ull, CountReels(), big, active);
    }
}

}  // namespace coop::dev::reelspam_probe
