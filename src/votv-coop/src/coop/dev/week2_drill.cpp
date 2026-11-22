// coop/dev/week2_drill.cpp -- [dev] the story's second week on three peers. See the header.

#include "coop/dev/week2_drill.h"

#include "coop/config/config.h"
#include "coop/dev/event_trigger.h"
#include "coop/dev/set_clock.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_drive.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/world/active_events.h"
#include "ue_wrap/world/daynightcycle.h"
#include "ue_wrap/world/economy.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace coop::dev::week2_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Arm() {
    static const std::string v = coop::config::ReadEnv("VOTVCOOP_WEEK2_DRILL");
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;
FVector g_spotA{}, g_spotB{};

std::vector<void*> Live(const wchar_t* cls) {
    std::vector<void*> out;
    for (void* o : R::FindObjectsByClass(cls))
        if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) out.push_back(o);
    return out;
}

float Dist(const FVector& a, const FVector& b) { return std::hypot(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }

bool WriteVec(void* obj, const wchar_t* field, const FVector& v) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), field) : -1;
    if (off < 0) return false;
    std::memcpy(static_cast<uint8_t*>(obj) + off, &v, sizeof(v));
    return true;
}

// HOST: the host's own player 300 m off along X, so only a client's player is near what follows.
void SendHostOff() {
    void* me = coop::players::Registry::Get().Local();
    FVector at{};
    if (!me || !E::TryGetActorLocation(me, at)) return;
    const bool ok = E::TeleportTo(me, {at.X + 30000.f, at.Y, at.Z + 200.f}, {0.f, 0.f, 0.f});
    UE_LOGI("[WEEK2] host: own player 300 m off (ok=%d)", ok ? 1 : 0);
}

// HOST: the scheduler's walk emptied (allEvents, as a client keeps it), then the day the Arirals eat on.
void SetEatingDay() {
    void* const ss = ue_wrap::daynightcycle::SaveSlotOfCycle(ue_wrap::daynightcycle::Cycle());
    const int32_t off = ss ? R::FindPropertyOffset(R::ClassOf(ss), L"allEvents") : -1;
    if (off >= 0) {
        struct Arr { void* data; int32_t num, max; };
        reinterpret_cast<Arr*>(static_cast<uint8_t*>(ss) + off)->num = 0;
    }
    coop::dev::set_clock::SetClock(13, 12, 0);
    UE_LOGI("[WEEK2] host: the event walk emptied (%d) and the day set to 13", off >= 0 ? 1 : 0);
}

bool Fire(const char* name) {
    for (const auto& ev : coop::dev::event_trigger::Events())
        if (std::strcmp(ev.name, name) == 0) return coop::dev::event_trigger::Trigger(ev);
    return false;
}

void* SpawnShrimp(const FVector& at) {
    void* cls = ue_wrap::object_index::ClassByName(L"prop_food_shrimp_C");
    if (!cls) cls = R::FindClass(L"prop_food_shrimp_C");
    return cls ? E::SpawnActor(cls, at) : nullptr;
}

void SetLastSpot(const FVector& at) {
    int n = 0;
    for (void* m : Live(L"shrimpEaterMaster_C")) n += WriteVec(m, L"lastloc", at) ? 1 : 0;
    UE_LOGI("[WEEK2] host: the eater's last spot set to (%.0f,%.0f,%.0f) on %d master(s)", at.X, at.Y, at.Z, n);
}

// Every shrimp pack, and every empty one (a prop_C whose `name` is shrimp_e), with those near the two spots.
void ShrimpCensus(uint64_t t) {
    const std::vector<void*> shrimps = Live(L"prop_food_shrimp_C");
    int nearA = 0, nearB = 0;
    for (void* o : shrimps) {
        FVector at{};
        if (!E::TryGetActorLocation(o, at)) continue;
        nearA += Dist(at, g_spotA) < 300.f ? 1 : 0;
        nearB += Dist(at, g_spotB) < 300.f ? 1 : 0;
    }
    int empties = 0;
    for (void* o : Live(L"prop_C")) {
        if (R::ClassNameOf(o) != L"prop_C") continue;
        const int32_t off = R::FindPropertyOffset(R::ClassOf(o), L"name");
        if (off < 0) continue;
        const auto* fn = reinterpret_cast<const R::FName*>(static_cast<const uint8_t*>(o) + off);
        if (R::ToString(*fn) == L"shrimp_e") ++empties;
    }
    UE_LOGI("[WEEK2] shrimp t=%llus: %zu shrimp pack(s), %d at spot A, %d at spot B, %d empty pack(s)",
            static_cast<unsigned long long>(t / 1000), shrimps.size(), nearA, nearB, empties);
}

void TickShrimp(bool host, int slot, uint64_t t) {
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        SetEatingDay();
    }
    if (host && g_step == 1 && t >= 40000u) {
        coop::RemotePlayer& rp = coop::puppet_drive::Puppet(1);
        FVector eye{};
        if (!rp.valid() || !rp.TryGetEyePosition(eye)) return;   // wait for slot 1
        g_step = 2;
        SendHostOff();
        g_spotA = {eye.X + 200.f, eye.Y, eye.Z};
        void* a = SpawnShrimp(g_spotA);
        UE_LOGI("[WEEK2] host: spot A beside slot 1 at (%.0f,%.0f,%.0f), shrimp %p", g_spotA.X, g_spotA.Y, g_spotA.Z,
                a);
        SetLastSpot(g_spotA);
        g_spotB = {eye.X - 15000.f, eye.Y, eye.Z + 100.f};
    }
    if (host && g_step == 2 && t >= 100000u) {
        g_step = 3;
        void* b = SpawnShrimp(g_spotB);
        UE_LOGI("[WEEK2] host: spot B 150 m from every player at (%.0f,%.0f,%.0f), shrimp %p", g_spotB.X, g_spotB.Y,
                g_spotB.Z, b);
        SetLastSpot(g_spotB);
    }
    (void)slot;
    if (t < 30000u || t > 160000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    ShrimpCensus(t);
}

void TickShips(bool host, uint64_t t) {
    // A save can hold an earlier picnic's arrows that its clearing never took (the rig's has two): cleared first, so
    // the picnic's own two are what every peer should count.
    if (host && g_step == 0 && t >= 25000u) {
        g_step = 10;
        UE_LOGI("[WEEK2] host: an earlier picnic cleared first (ok=%d)", Fire("destroyPicnic") ? 1 : 0);
    }
    if (host && g_step == 10 && t >= 40000u) {
        g_step = 1;
        UE_LOGI("[WEEK2] host: the picnic fired (ok=%d)", Fire("picnic") ? 1 : 0);
    }
    if (host && g_step == 1 && t >= 200000u) {
        g_step = 2;
        UE_LOGI("[WEEK2] host: the picnic cleared (ok=%d)", Fire("destroyPicnic") ? 1 : 0);
    }
    if (t < 20000u || t > 260000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 10000u;
    UE_LOGI("[WEEK2] ships t=%llus: %zu warp arrow(s)", static_cast<unsigned long long>(t / 1000),
            Live(L"arirShipVanishing_C").size());
}

void TickKnock(bool host, int slot, uint64_t t) {
    static const FVector kKnocker{-260.f, 2815.f, 6690.f};   // the eventer's own spawn of kocker_C
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        SendHostOff();
    }
    if (host && g_step == 1 && t >= 40000u) {
        g_step = 2;
        UE_LOGI("[WEEK2] host: the knocker fired (ok=%d)", Fire("ventKnocker") ? 1 : 0);
    }
    if (!host && slot == 1 && g_step == 0 && t >= 60000u) {
        g_step = 1;
        void* me = coop::players::Registry::Get().Local();
        // The knocker's box: a metre cube in front of the main computer, (250,1800,6350) +- 50 on the rig, read above.
        const FVector to{250.f, 1800.f, 6360.f};
        const bool ok = me && (E::TeleportTo(me, to, {0.f, 0.f, 0.f}) || E::SetActorLocation(me, to));
        UE_LOGI("[WEEK2] slot 1: its player to the knocker (ok=%d)", ok ? 1 : 0);
    }
    static bool s_bounds = false;
    if (host && !s_bounds) {
        const std::vector<void*> k = Live(L"kocker_C");
        FVector o{}, ext{};
        if (!k.empty() && E::GetActorBounds(k[0], true, o, ext)) {
            s_bounds = true;
            UE_LOGI("[WEEK2] host: the knocker's box spans (%.0f,%.0f,%.0f) +- (%.0f,%.0f,%.0f)", o.X, o.Y, o.Z,
                    ext.X, ext.Y, ext.Z);
        }
    }
    if (t < 30000u || t > 120000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 2000u;
    void* me = coop::players::Registry::Get().Local();
    FVector at{};
    const float d = me && E::TryGetActorLocation(me, at) ? Dist(at, kKnocker) : -1.f;
    int32_t events = -1;
    if (ue_wrap::active_events::EnsureResolved()) ue_wrap::active_events::ReadCount(events);
    UE_LOGI("[WEEK2] knock t=%llus: %zu knocker(s) here, own player %.0f cm from it, %d event(s) counted here",
            static_cast<unsigned long long>(t / 1000), Live(L"kocker_C").size(), d, static_cast<int>(events));
}

// lib_C's addAriralRep, as a caller in the world runs it.
bool AddRep(float v) {
    void* const lib = R::FindClassDefaultObject(L"lib_C");
    void* const cls = ue_wrap::object_index::ClassByName(L"lib_C");
    void* ctx = coop::players::Registry::Get().Local();
    ue_wrap::ParamFrame f(cls ? R::FindFunction(cls, L"addAriralRep") : nullptr);
    return lib && ctx && f.valid() && f.Set<float>(L"rep", v) && f.Set<void*>(L"__WorldContext", ctx) &&
           ue_wrap::Call(lib, f);
}

float ReadRep() {
    void* const ss = ue_wrap::economy::SaveSlotPtr();
    const int32_t off = ss ? R::FindPropertyOffset(R::ClassOf(ss), L"arirReputation") : -1;
    float v = -9999.f;
    if (off >= 0) std::memcpy(&v, static_cast<uint8_t*>(ss) + off, sizeof(v));
    return v;
}

void TickRep(bool host, int slot, uint64_t t) {
    if (host && g_step == 0 && t >= 40000u) {
        g_step = 1;
        UE_LOGI("[WEEK2] host: +7 reputation (ran=%d)", AddRep(7.f) ? 1 : 0);
    }
    if (!host && slot == 1 && g_step == 0 && t >= 50000u) {
        g_step = 1;
        UE_LOGI("[WEEK2] slot 1: +100 reputation of its own (ran=%d)", AddRep(100.f) ? 1 : 0);
    }
    if (t < 30000u || t > 100000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    UE_LOGI("[WEEK2] rep t=%llus: %.2f", static_cast<unsigned long long>(t / 1000), ReadRep());
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Arm().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[WEEK2] drill armed slot=%d arm=%s", slot, Arm().c_str());
    }
    const uint64_t t = Now() - g_startAt;
    if (Arm() == "shrimp") TickShrimp(host, slot, t);
    else if (Arm() == "ships") TickShips(host, t);
    else if (Arm() == "knock") TickKnock(host, slot, t);
    else if (Arm() == "rep") TickRep(host, slot, t);
}

}  // namespace coop::dev::week2_drill
