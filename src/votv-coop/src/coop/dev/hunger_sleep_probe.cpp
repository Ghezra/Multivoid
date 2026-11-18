// coop/dev/hunger_sleep_probe.cpp -- [dev] a hungry player tries to sleep. See the header.
//
// EVERY PEER, from 30 s after its session connects: its food is set to 5, and at 32, 40, 48 and 56 s the game's own
// sleep (mainGamemode_C::sleep on the bed nearest its player; at 48 and 56 s through the bed's own use) runs, whose food
// check shows "Im too hungry to sleep". Its sleep need is set to 5 too, so the bed's own check lets it through.
// Each second from 30 s to 64 s: the food and the sleeping flag as the save slot holds them (`[HUNGERSLEEP]`). Run with
// VOTVCOOP_FEEDBACK_PROBE=1 to see each hint the attempts make.

#include "coop/dev/hunger_sleep_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/sleep.h"
#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <cstdint>

namespace coop::dev::hunger_sleep_probe {
namespace {

namespace V = ue_wrap::vitals;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_HUNGER_SLEEP_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_tries = 0;
bool g_fed = false;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const char* role = session->role() == coop::net::Role::Host ? "host" : "client";
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[HUNGERSLEEP] probe armed role=%s", role);
    }
    const uint64_t t = Now() - g_startAt;
    if (!g_fed && t >= 30000u) {
        g_fed = true;
        UE_LOGI("[HUNGERSLEEP] %s: food set to 5 (ok=%d), sleep to 5 (ok=%d)", role, V::Write(V::Field::Food, 5.f) ? 1 : 0,
                V::Write(V::Field::Sleep, 5.f) ? 1 : 0);
    }
    if (g_fed && g_tries < 4 && t >= 32000u + 8000u * g_tries) {
        ++g_tries;
        void* me = coop::players::Registry::Get().Local();
        ue_wrap::FVector at{};
        if (me) ue_wrap::engine::TryGetActorLocation(me, at);
        void* bed = me ? ue_wrap::sleep::NearestBed(at) : nullptr;
        float food = -1.f;
        V::Read(V::Field::Food, &food);
        // Tries 1 and 2: the game's sleep itself. Tries 3 and 4: the bed's own use, as the player's press runs it (its
        // actionOptionIndex with the sleep action, 4), which checks the sleep need before it calls the game's sleep.
        bool ran = false;
        if (bed && g_tries <= 2) ran = ue_wrap::sleep::CallSleep(bed);
        else if (bed) {
            void* f = ue_wrap::reflection::FindFunction(ue_wrap::reflection::ClassOf(bed), L"actionOptionIndex");
            ue_wrap::ParamFrame pf(f);
            ran = f && pf.valid() && pf.Set<void*>(L"player", me) && pf.Set<uint8_t>(L"action", 4) &&
                  ue_wrap::Call(bed, pf);
        }
        UE_LOGI("[HUNGERSLEEP] %s: try %d (%s) -- food %.1f, bed %p, ran=%d", role, g_tries,
                g_tries <= 2 ? "the game's sleep" : "the bed's use", food, bed, ran ? 1 : 0);
    }
    if (t >= 30000u && t < 64000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 1000;
        float food = -1.f, sleeping = -1.f;
        V::Read(V::Field::Food, &food);
        V::Read(V::Field::Sleeping, &sleeping);
        UE_LOGI("[HUNGERSLEEP] %s t=%llu: food %.1f sleeping %.0f", role, t / 1000ull, food, sleeping);
    }
}

}  // namespace coop::dev::hunger_sleep_probe
