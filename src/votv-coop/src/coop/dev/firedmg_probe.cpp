// coop/dev/firedmg_probe.cpp -- [dev] a client set on fire, every player's health through the burn
// (VOTVCOOP_FIREDMG_PROBE=1; VOTVCOOP_FIREDMG_FUEL seconds of fire, default 8).
//
// The card: a burning player took no damage, and the other player saw the damage on its own screen. THE FIRST CLIENT
// (slot 1), once its world is settled and its spawn protection is over, and 25 s after it connected: lights itself
// through mainPlayer_C's own ignite with FUEL seconds. EVERY PEER, every half second from 20 s to 50 s after its own
// connect: its own health, max health, burning and burning time (`[FIREDMG]`). The host's puppet flame says itself
// in remote_player's own "flame on/off" line.

#include "coop/dev/firedmg_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/death_revive.h"
#include "coop/player/players_registry.h"
#include "coop/session/net_pump.h"

#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_player_verbs.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <string>

namespace coop::dev::firedmg_probe {
namespace {

namespace E = ue_wrap::engine;
namespace V = ue_wrap::vitals;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_FIREDMG_PROBE") == "1";
    return on;
}

float Fuel() {
    static const float f = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_FIREDMG_FUEL");
        return v.empty() ? 8.f : static_cast<float>(std::atof(v.c_str()));
    }();
    return f;
}

float Read(V::Field f) {
    float v = -1.f;
    return V::Read(f, &v) ? v : -1.f;
}

uint64_t g_startAt = 0, g_nextLog = 0;
bool g_lit = false;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) {
        if (On() && g_startAt && session && !session->connected()) { g_startAt = 0; g_lit = false; }
        return;
    }
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[FIREDMG] probe armed role=%s slot=%u fuel=%.0f", host ? "host" : "client",
                static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId()), Fuel());
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    if (!host && !g_lit && t >= 25000u && coop::players::Registry::Get().LocalPeerId() == 1 && coop::net_pump::IsWorldSettled() &&
        coop::death_revive::ArmReadyAfterPawnMs() >= 0) {
        bool invinc = true;
        if (E::ReadMainPlayerStartInvinc(me, invinc) && !invinc) {
            g_lit = true;
            const bool ran = E::InvokeMainPlayerIgnite(me, Fuel());
            UE_LOGI("[FIREDMG] client: lit itself (ran=%d) at health %.1f", ran ? 1 : 0, Read(V::Field::Health));
        }
    }
    if (t >= 20000u && t < 50000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 500;
        UE_LOGI("[FIREDMG] %s t=%.1f: health %.2f of %.0f, burning %.0f, burn time %.1f", host ? "host" : "client",
                t / 1000.0, Read(V::Field::Health), Read(V::Field::MaxHealth), Read(V::Field::Burning),
                Read(V::Field::BurningTime));
    }
}

}  // namespace coop::dev::firedmg_probe
