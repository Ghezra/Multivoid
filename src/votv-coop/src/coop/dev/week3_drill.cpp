// coop/dev/week3_drill.cpp -- [dev] the story's third week on three peers. See the header.

#include "coop/dev/week3_drill.h"

#include "coop/config/config.h"
#include "coop/dev/event_trigger.h"
#include "coop/dev/set_clock.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/prop_use.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/world/daynightcycle.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace coop::dev::week3_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Arm() {
    static const std::string v = coop::config::ReadEnv("VOTVCOOP_WEEK3_DRILL");
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;

std::vector<void*> Live(const wchar_t* cls) {
    std::vector<void*> out;
    for (void* o : R::FindObjectsByClass(cls))
        if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) out.push_back(o);
    return out;
}

bool Fire(const char* name) {
    for (const auto& ev : coop::dev::event_trigger::Events())
        if (std::strcmp(ev.name, name) == 0) return coop::dev::event_trigger::Trigger(ev);
    return false;
}

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return nullptr;
    void* v = nullptr;
    std::memcpy(&v, static_cast<const uint8_t*>(obj) + off, sizeof(v));
    return v && R::IsLive(v) ? v : nullptr;
}

bool Visible(void* ship) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!ship || !R::FindBoolProperty(R::ClassOf(ship), L"visible", off, mask)) return false;
    return (static_cast<const uint8_t*>(ship)[off] & mask) != 0;
}

void TickLantern(bool host, int slot, uint64_t t) {
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        UE_LOGI("[WEEK3] host: the lantern prank fired (ok=%d)", Fire("arirBuster") ? 1 : 0);
    }
    // Beside the notepad: four spots round it at the crates' floor, each kept only if the player still stands near it
    // 2 s later (inside the building's walls the body falls out of the world), then the use 3 s after the move, so the
    // host's copy of this player stands where the use is judged.
    static const float kSpots[4][2] = {{0.f, 110.f}, {0.f, -110.f}, {110.f, 0.f}, {-110.f, 0.f}};
    static int s_spot = 0;
    static uint64_t s_movedAt = 0;
    if (!host && slot == 1 && g_step < 2 && t >= 50000u) {
        const std::vector<void*> notes = Live(L"prop_notebook_busterSpawner_C");
        void* me = coop::players::Registry::Get().Local();
        FVector at{}, mine{};
        if (notes.empty() || !me || !E::TryGetActorLocation(notes[0], at) || !E::TryGetActorLocation(me, mine)) return;
        const float d = std::sqrt((mine.X - at.X) * (mine.X - at.X) + (mine.Y - at.Y) * (mine.Y - at.Y) +
                                  (mine.Z - at.Z) * (mine.Z - at.Z));
        if (g_step == 1 && Now() - s_movedAt >= 2000u) {
            if (d < 300.f) {
                if (Now() - s_movedAt >= 3000u) {
                    g_step = 2;
                    UE_LOGI("[WEEK3] slot 1: its use of the notepad called from %.0f cm (ran=%d)", d,
                            ue_wrap::prop_use::CallActionOptionIndex(notes[0], me, nullptr, at, 4, nullptr) ? 1 : 0);
                }
                return;
            }
            g_step = 0;
            if (++s_spot >= 4) {
                g_step = 3;
                UE_LOGW("[WEEK3] slot 1: no spot beside the notepad held its player");
                return;
            }
        }
        if (g_step == 0) {
            g_step = 1;
            s_movedAt = Now();
            const FVector to{at.X + kSpots[s_spot][0], at.Y + kSpots[s_spot][1], at.Z};
            UE_LOGI("[WEEK3] slot 1: to spot %d beside the notepad (ok=%d)", s_spot,
                    (E::TeleportTo(me, to, {0.f, 0.f, 0.f}) || E::SetActorLocation(me, to)) ? 1 : 0);
        }
    }
    if (t < 25000u || t > 120000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    const std::vector<void*> slappers = Live(L"arirSlapper_C");
    FVector at{};
    if (!slappers.empty()) E::TryGetActorLocation(slappers[0], at);
    UE_LOGI("[WEEK3] lantern t=%llus: %zu notepad(s), %zu door Ariral(s) at (%.0f,%.0f,%.0f)",
            static_cast<unsigned long long>(t / 1000), Live(L"prop_notebook_busterSpawner_C").size(), slappers.size(),
            at.X, at.Y, at.Z);
}

void TickTower(bool host, int slot, uint64_t t) {
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        void* const ss = ue_wrap::daynightcycle::SaveSlotOfCycle(ue_wrap::daynightcycle::Cycle());
        const int32_t off = ss ? R::FindPropertyOffset(R::ClassOf(ss), L"allEvents") : -1;
        if (off >= 0) {
            struct Arr { void* data; int32_t num, max; };
            reinterpret_cast<Arr*>(static_cast<uint8_t*>(ss) + off)->num = 0;
        }
        coop::dev::set_clock::SetClock(22, 22, 0);
        UE_LOGI("[WEEK3] host: the event walk emptied (%d) and the clock set to day 22, 22:00", off >= 0 ? 1 : 0);
    }
    // Into the ship's box: four spots round its centre, each kept only if the player still stands in the box 2 s later
    // (a body moved into the mast is sent below the world by the game's clip check), facing away from the ship.
    static const float kSpots[5][2] = {{150.f, 0.f}, {-150.f, 0.f}, {0.f, 150.f}, {0.f, -150.f}, {0.f, 0.f}};
    static int s_spot = 0;
    static uint64_t s_movedAt = 0;
    if (!host && slot == 1 && g_step < 2 && t >= 40000u) {
        const std::vector<void*> ships = Live(L"arirShip_tower_C");
        void* const box = ships.empty() ? nullptr : Field(ships[0], L"Box");
        void* me = coop::players::Registry::Get().Local();
        FVector at{}, ship{}, mine{};
        if (!box || !me || !ue_wrap::engine::TryGetComponentLocation(box, at) ||
            !E::TryGetActorLocation(ships[0], ship) || !E::TryGetActorLocation(me, mine))
            return;
        if (g_step == 1 && Now() - s_movedAt >= 2000u) {
            if (ue_wrap::engine::IsOverlappingActor(box, me)) {
                g_step = 2;
                UE_LOGI("[WEEK3] slot 1: its player stands in the ship's box at (%.0f,%.0f,%.0f)", mine.X, mine.Y,
                        mine.Z);
                return;
            }
            g_step = 0;
            if (++s_spot >= 5) {
                g_step = 3;
                UE_LOGW("[WEEK3] slot 1: no spot in the ship's box held its player");
                return;
            }
        }
        if (g_step == 0) {
            g_step = 1;
            s_movedAt = Now();
            const FVector to{at.X + kSpots[s_spot][0], at.Y + kSpots[s_spot][1], at.Z};
            const float yaw = std::atan2(to.Y - ship.Y, to.X - ship.X) * 57.2958f;
            UE_LOGI("[WEEK3] slot 1: to spot %d in the ship's box (ok=%d)", s_spot,
                    (E::SetActorLocation(me, to) && E::SetActorRotation(me, {0.f, yaw, 0.f})) ? 1 : 0);
        }
    }
    // Its own copy's roll, as the game's loop makes it when its 10% comes up: shown here alone before, refused after.
    if (!host && slot == 1 && g_step == 2 && t >= 150000u) {
        g_step = 4;
        const std::vector<void*> ships = Live(L"arirShip_tower_C");
        ue_wrap::ParamFrame f(ships.empty() ? nullptr : R::FindDispatchFunctionCached(R::ClassOf(ships[0]), L"appear"));
        UE_LOGI("[WEEK3] slot 1: its own ship's appear(true) called (ran=%d)",
                f.valid() && f.Set<bool>(L"dir", true) && ue_wrap::Call(ships[0], f) ? 1 : 0);
    }
    if (t < 25000u || t > 240000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    const std::vector<void*> ships = Live(L"arirShip_tower_C");
    int32_t hour = -1, minute = -1, day = -1;
    ue_wrap::daynightcycle::ReadTimeZ(hour, minute, day);
    UE_LOGI("[WEEK3] tower t=%llus: day %d %02d:%02d, %zu ship(s), shown=%d, %zu dreamer UFO(s)",
            static_cast<unsigned long long>(t / 1000), day, hour, minute, ships.size(),
            ships.empty() ? -1 : (Visible(ships[0]) ? 1 : 0), Live(L"ufoDreamer_C").size());
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Arm().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[WEEK3] drill armed slot=%d arm=%s", slot, Arm().c_str());
    }
    const uint64_t t = Now() - g_startAt;
    if (Arm() == "lantern") TickLantern(host, slot, t);
    else if (Arm() == "tower") TickTower(host, slot, t);
}

}  // namespace coop::dev::week3_drill
