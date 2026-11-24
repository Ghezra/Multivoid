// coop/dev/entity_probe.cpp -- [dev] the wiki's extraterrestrials on every peer (VOTVCOOP_ENTITY_PROBE=1 on every
// peer). HOST: from 35 s, every 20 s the next class of the list is loaded and spawned 30 m before its player, and
// destroyed 12 s on (`[ENTITY] host: spawn`, `destroy`). Every peer, each 4 s (`[ENTITY]`): its live count of each
// class of the list that has one, so a class the host spawned and a client never counted, or kept, is seen.
// VOTVCOOP_ENTITY_PROBE=real: the game's own spawns instead -- 35 s the super fog as its event spawns it (its own
// spawnUfo mints the Thunder Wisp), 75 s the signal screen's spawn_trifo (the TriFO).

#include "coop/dev/entity_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/session/teleport_client.h"
#include "coop/world/world_actor_sync.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_physics.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/actors/spawner_stamp.h"
#include "ue_wrap/engine/data_table.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>

namespace coop::dev::entity_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
namespace CR = ue_wrap::class_resolve;

constexpr const wchar_t* kClasses[] = {
    L"npc_ariral_pigBeater_C", L"npc_ariral_shooter_C", L"arirSlapper_C",  L"npc_arirFollower_C", L"antibreather_C",
    L"antibreather_angry_C",   L"tentacleBall_C",       L"kavotia_C",      L"igetis_C",           L"fossilhound_C",
    L"grayTest_C",             L"grayboar_C",           L"grayboar_overcharged_C", L"firetank_C",  L"geomOcta_C",
    L"theEvil_C",              L"ufo_ballfo_C",         L"ufo_boofo_C",    L"ufo_joel_C",         L"ufo_pillfo_C",
    L"ufo_trifo_C",            L"jellyfish_C",          L"HoelUfo_C",      L"soltomiaCleaning_C", L"piramid2_C",
};
constexpr size_t kCount = sizeof(kClasses) / sizeof(kClasses[0]);

constexpr const wchar_t* kAnom[] = {
    L"badSun_C",       L"hexpillar_C",       L"prop_hexpillar_C", L"log_C",             L"npc_goreSlither_C",
    L"theBody_C",      L"prop_wMannequin_C", L"prop_mannequin_C", L"prop_impostor_C",   L"npc_funguy_C",
    L"prop_duende_C",  L"tomtenisse_C",      L"eyer_C",           L"puddle_C",
    L"figura_C",       L"gost_C",            L"skerfuroWalk_C",   L"teleBooth_C",
    L"walkingTree_C",  L"goat_C",            L"centipedeHead_C",  L"npc_zombie_C",      L"npc_zombie_deer_C",
};
constexpr const wchar_t* kParts[] = {L"comp_emf_C", L"comp_photographic_C", L"comp_paranormal_C"};

bool Armed() {
    static const bool v = !coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE").empty();
    return v;
}

// VOTVCOOP_ENTITY_PROBE=anom: the wiki's Anomalous list instead. Every peer, each 10 s from 30 s (`[ANOM]`): each
// class's count and points (10 m), and the classes owning an EMF, photographic or paranormal part; the host spawns
// and destroys the list's classes as above, but not the Killer Birch nor the Red Void Orb (a raw birch_C or
// npc_orborb_C ends the game, wanting its spawner's stamp). Every peer, at 45 s: the wanderers' own rolls (the log's
// and the sheet ghost's Teleport, the booth's Timer, William's lookPlace), the gamemode's tickerBody and tickerFunguy
// and the Mimic's gen, many times, as each machine's own timers would.
bool Anom() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "anom";
    return v;
}
const wchar_t* const* List() { return Anom() ? kAnom : kClasses; }
size_t ListCount() { return Anom() ? sizeof(kAnom) / sizeof(kAnom[0]) : sizeof(kClasses) / sizeof(kClasses[0]); }

// VOTVCOOP_ENTITY_PROBE=four: the basalt pillars, Wolfgang, the Nisse and the Killer Birch on the game's own paths.
// HOST 40 s: slot 1 beside the basalt stone; CLIENT slot 1, 55 s: its own digUp of it. HOST 70 s: the pillar's `a` at
// 0.95, so its own tick spreads it; 80 s: the cave spawner's cencipede; CLIENT slot 1, 90 s: its own. HOST 100 s: a
// Christmas tree and a Nisse beside it; CLIENT slot 1, 115 s: its tree's night marked and its woken. HOST 130 s: a
// birch, stamped as its spawner does, 25 m behind the host, angry and looked away from, and slot 1 moved into it.
// Every peer, each 10 s to 300 s (`[FOUR]`): each class's count and how many are mirrors.

// VOTVCOOP_ENTITY_PROBE=lamps: a store prop's own on switch. HOST 40 s: a desk lamp, a floor lamp, a beacon, a spotlight
// head, a hot plate (powered, so its heat runs while it is on) and a tripod spotlight (its head's switch), 1.2 m apart
// and 2.5 m before the host. CLIENT slot 1, 60 s: each one's switch on its copy (its right-click, else its radial action
// 4; the plate's is action 8). HOST 80 s: each one's switch on the host's, but the plate's level is stepped (action 5)
// and its switch left on, as a slot that joins after finds it. Every peer, each 5 s to 110 s (`[LAMPS]`): each one's
// switch, and the plate's `powered` and level.
bool Lamps() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "lamps";
    return v;
}

void* First(const wchar_t* cls);

constexpr const wchar_t* kLamps[] = {L"prop_deskLamp_C", L"prop_floorlamp_C", L"prop_beacon_C", L"prop_spotlightHead_C",
                                     L"prop_cookingPad_C", L"prop_spotlight_C"};

// The switched actor of a drill row: the tripod spotlight's is its head, a child actor (its `lighhead`).
void* Target(const wchar_t* c) {
    void* const a = First(c);
    if (!a || std::wcscmp(c, L"prop_spotlight_C") != 0) return a;
    static R::InstanceOffset s_head(L"lighhead");
    const int32_t off = s_head.Of(a);
    void* const h = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(a) + off) : nullptr;
    return h && R::IsLive(h) ? h : nullptr;
}

int BoolOf(void* a, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!a || !R::FindBoolProperty(R::ClassOf(a), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(a)[off] & mask) ? 1 : 0;
}
int LevelOfPad() {
    void* const a = First(L"prop_cookingPad_C");
    static R::InstanceOffset s_power(L"power");
    const int32_t off = a ? s_power.Of(a) : -1;
    return off >= 0 ? *reinterpret_cast<const int32_t*>(static_cast<const char*>(a) + off) : -1;
}
int ActiveOf(void* a) {
    const int v = BoolOf(a, L"active");
    return v >= 0 ? v : BoolOf(a, L"isActive");
}

// The switch a player presses: the right-click where the prop has one, else its radial action 4.
bool Press(void* a, void* me, uint8_t action = 0) {
    void* f = action ? nullptr : R::FindDispatchFunction(R::ClassOf(a), L"playerHandUse_RMB", nullptr);
    if (!f) {
        ue_wrap::ParamFrame pf(R::FindDispatchFunction(R::ClassOf(a), L"actionOptionIndex", nullptr));
        if (!pf.valid()) return false;
        pf.Set<void*>(L"player", me);
        const uint8_t act = action ? action : 4;
        pf.Set(L"action", act);
        return ue_wrap::Call(a, pf);
    }
    ue_wrap::ParamFrame pf(f);
    if (!pf.valid()) return false;
    pf.Set<void*>(L"player", me);
    return ue_wrap::Call(a, pf);
}

void LampsStep(bool host, uint64_t t) {
    static int s_step = 0;
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector at{};
    ue_wrap::FRotator rot{};
    if (me) {
        E::TryGetActorLocation(me, at);
        E::TryGetActorRotation(me, rot);
    }
    const float yaw = rot.Yaw * 0.0174532925f;
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        int k = 0;
        for (const wchar_t* c : kLamps) {
            const CR::Resolved rc = CR::ResolveClass(c, CR::AnyFamily(), CR::Load::Immediate);
            const float side = (static_cast<float>(k++) - 2.f) * 120.f;
            const ue_wrap::FVector to{at.X + 250.f * std::cos(yaw) - side * std::sin(yaw),
                                      at.Y + 250.f * std::sin(yaw) + side * std::cos(yaw), at.Z};
            void* const a = rc.cls ? E::SpawnActor(rc.cls, to) : nullptr;
            int32_t off = -1;
            uint8_t mask = 0;
            if (a && std::wcscmp(c, L"prop_cookingPad_C") == 0 && R::FindBoolProperty(R::ClassOf(a), L"powered", off, mask)) {
                static_cast<uint8_t*>(a)[off] |= mask;  // as a plugged cord leaves it
                void* const upd = R::FindDispatchFunction(R::ClassOf(a), L"upd", nullptr);
                if (upd) R::CallFunction(a, upd, nullptr);
            }
            UE_LOGI("[LAMPS] host: %ls spawned=%d", c, a ? 1 : 0);
        }
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if ((slot1 && s_step == 0 && t >= 60000u) || (host && s_step == 1 && t >= 80000u)) {
        s_step = host ? 2 : 1;
        for (const wchar_t* c : kLamps) {
            const bool pad = std::wcscmp(c, L"prop_cookingPad_C") == 0;  // its switch is action 8; the host steps its level (5)
            void* const a = Target(c);
            const int before = ActiveOf(a);
            const bool ran = a && Press(a, me, pad ? (host ? 5 : 8) : 0);
            UE_LOGI("[LAMPS] %s pressed %ls: ran=%d active %d -> %d", host ? "host" : "client", c, ran ? 1 : 0, before,
                    ActiveOf(a));
        }
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 110000u || t < s_next) return;
    s_next = t + 5000u;
    std::wstring line;
    for (const wchar_t* c : kLamps) line += std::wstring(c) + L"=" + std::to_wstring(ActiveOf(Target(c))) + L" ";
    UE_LOGI("[LAMPS] %s t=%llu: (active, -1 none) %ls pad_powered=%d pad_level=%d", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), line.c_str(), BoolOf(First(L"prop_cookingPad_C"), L"powered"), LevelOfPad());
    if (t >= 105000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=churn: the smoke detector and the butter churn. HOST 40 s: one of each 2.5 m before the host;
// 45 s: the churn given 2 of milk (a full carton's 100 takes some 170 cranks) and the detector's alarm on, as its check
// finds a fire. CLIENT slot 1, 60 s: its copy's silence (action 5); 62 s to 67 s: ten cranks (action 4); 70 s: its
// copy's own alarm on, which the host refuses. HOST 80 s: the alarm on again (`[CHURN] host: the alarm on again`), so
// a slot that joins after finds it on, and the churn's butter. CLIENT slot 1, 150 s: the take (action 7). Every peer,
// each 5 s to 200 s (`[CHURN]`): the alarm's `isActive`, the churn's `speed`, `milkPercent` and `hasButter`.
bool ChurnMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "churn";
    return v;
}

bool Alarm(void* a, bool on) {
    ue_wrap::ParamFrame pf(a ? R::FindDispatchFunction(R::ClassOf(a), L"alarm", nullptr) : nullptr);
    if (!pf.valid()) return false;
    pf.Set<bool>(L"activate", on);
    return ue_wrap::Call(a, pf);
}

float FloatOf(void* a, const wchar_t* name) {
    R::InstanceOffset o(name);
    const int32_t off = a ? o.Of(a) : -1;
    return off >= 0 ? *reinterpret_cast<const float*>(static_cast<const char*>(a) + off) : -1.f;
}

void ChurnStep(bool host, uint64_t t) {
    static int s_step = 0;
    static uint64_t s_nextCrank = 0;
    static int s_cranks = 0;
    void* const me = coop::players::Registry::Get().Local();
    void* const det = First(L"prop_smokeDetec_C");
    void* const churn = First(L"prop_butterchurn_C");
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        if (me) {
            E::TryGetActorLocation(me, at);
            E::TryGetActorRotation(me, rot);
        }
        const float yaw = rot.Yaw * 0.0174532925f;
        int k = 0;
        for (const wchar_t* c : {L"prop_smokeDetec_C", L"prop_butterchurn_C"}) {
            const CR::Resolved rc = CR::ResolveClass(c, CR::AnyFamily(), CR::Load::Immediate);
            const float side = (static_cast<float>(k++) - 0.5f) * 150.f;
            const ue_wrap::FVector to{at.X + 250.f * std::cos(yaw) - side * std::sin(yaw),
                                      at.Y + 250.f * std::sin(yaw) + side * std::cos(yaw), at.Z};
            void* const a = rc.cls ? E::SpawnActor(rc.cls, to) : nullptr;
            UE_LOGI("[CHURN] host: %ls spawned=%d", c, a ? 1 : 0);
        }
    }
    if (host && s_step == 1 && t >= 45000u) {
        s_step = 2;
        R::InstanceOffset o(L"milkPercent");  // the spawn is listed by the next frame, not its own
        const int32_t off = churn ? o.Of(churn) : -1;
        if (off >= 0) *reinterpret_cast<float*>(static_cast<char*>(churn) + off) = 2.f;
        UE_LOGI("[CHURN] host: the alarm on (ran=%d)", Alarm(det, true) ? 1 : 0);
    }
    if (host && s_step == 2 && t >= 80000u) {
        s_step = 3;
        UE_LOGI("[CHURN] host: the alarm on again (ran=%d)", Alarm(det, true) ? 1 : 0);
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if (slot1 && s_step == 0 && t >= 60000u && det) {
        s_step = 1;
        UE_LOGI("[CHURN] client: silenced its alarm copy (ran=%d)", det && Press(det, me, 5) ? 1 : 0);
        s_nextCrank = t + 2000u;
    }
    if (slot1 && s_step == 1 && churn && t >= s_nextCrank) {
        s_nextCrank = t + 500u;
        const bool ran = Press(churn, me, 4);
        UE_LOGI("[CHURN] client: crank %d (ran=%d)", ++s_cranks, ran ? 1 : 0);
        if (s_cranks >= 10) s_step = 2;
    }
    if (slot1 && s_step == 2 && t >= 70000u) {
        s_step = 3;
        UE_LOGI("[CHURN] client: its alarm copy's own on (ran=%d)", Alarm(det, true) ? 1 : 0);
    }
    if (slot1 && s_step == 3 && t >= 150000u) {
        s_step = 4;
        UE_LOGI("[CHURN] client: took the butter (ran=%d)", churn && Press(churn, me, 7) ? 1 : 0);
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 200000u || t < s_next) return;
    s_next = t + 5000u;
    int butter = -1;
    if (churn) butter = BoolOf(churn, L"hasButter");
    UE_LOGI("[CHURN] %s t=%llu: alarm=%d churn speed=%.2f milk=%.2f butter=%d", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), det ? BoolOf(det, L"isActive") : -1, FloatOf(churn, L"speed"),
            FloatOf(churn, L"milkPercent"), butter);
    if (t >= 195000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=drone: the remote drone and the sled. HOST 40 s: one of each 2.5 m before the host, the drone's
// energy 100. CLIENT slot 1, 55 s: its copy's switch (action 8); 60 s to 66 s: `move` up, then 66 s to 72 s forward,
// a call each 100 ms as the laptop's sliders make them; 80 s: the switch again. CLIENT slot 1, 90 s: sits on the sled
// (its own playerSit, its player this one), 91 s to 95 s its forward held (moveForward) and, at 91 s,
// a shove of 6 m/s along x as a slope would give (on flat ground the push loses to friction), 96 s up (playerUnsit); HOST
// 110 s to 116 s, the same on its own, shoved back at 111 s. Every
// peer, each 2 s to 140 s (`[DRONE]`): the drone's place, `active` and `energy`, and the sled's place.
bool DroneMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "drone";
    return v;
}

bool CallWith(void* a, const wchar_t* fn, const wchar_t* p0, float v0, const wchar_t* p1 = nullptr, float v1 = 0.f,
              const wchar_t* p2 = nullptr, float v2 = 0.f) {
    ue_wrap::ParamFrame pf(a ? R::FindDispatchFunction(R::ClassOf(a), fn, nullptr) : nullptr);
    if (!pf.valid()) return false;
    pf.Set<float>(p0, v0);
    if (p1) pf.Set<float>(p1, v1);
    if (p2) pf.Set<float>(p2, v2);
    return ue_wrap::Call(a, pf);
}

bool CallPlayer(void* a, const wchar_t* fn, void* player) {
    ue_wrap::ParamFrame pf(a ? R::FindDispatchFunction(R::ClassOf(a), fn, nullptr) : nullptr);
    if (!pf.valid()) return false;
    pf.Set<void*>(L"player", player);
    return ue_wrap::Call(a, pf);
}

bool CallPressed(void* a, const wchar_t* fn, bool pressed) {
    ue_wrap::ParamFrame pf(a ? R::FindDispatchFunction(R::ClassOf(a), fn, nullptr) : nullptr);
    if (!pf.valid()) return false;
    pf.Set<bool>(L"pressed", pressed);
    return ue_wrap::Call(a, pf);
}

void DroneStep(bool host, uint64_t t) {
    static int s_step = 0;
    static uint64_t s_nextMove = 0;
    void* const me = coop::players::Registry::Get().Local();
    void* const drone = First(L"prop_rdrone_C");
    void* const sled = First(L"prop_sled_C");
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        if (me) {
            E::TryGetActorLocation(me, at);
            E::TryGetActorRotation(me, rot);
        }
        const float yaw = rot.Yaw * 0.0174532925f;
        int k = 0;
        for (const wchar_t* c : {L"prop_rdrone_C", L"prop_sled_C"}) {
            const CR::Resolved rc = CR::ResolveClass(c, CR::AnyFamily(), CR::Load::Immediate);
            const float side = (static_cast<float>(k++) - 0.5f) * 200.f;
            const ue_wrap::FVector to{at.X + 250.f * std::cos(yaw) - side * std::sin(yaw),
                                      at.Y + 250.f * std::sin(yaw) + side * std::cos(yaw), at.Z};
            UE_LOGI("[DRONE] host: %ls spawned=%d", c, rc.cls && E::SpawnActor(rc.cls, to) ? 1 : 0);
        }
    }
    if (host && s_step == 1 && t >= 42000u && drone) {
        s_step = 2;
        const float e = FloatOf(drone, L"energy");
        R::InstanceOffset o(L"energy");  // the spawn is listed by the next frame, not its own
        const int32_t off = o.Of(drone);
        if (off >= 0) *reinterpret_cast<float*>(static_cast<char*>(drone) + off) = 100.f;
        UE_LOGI("[DRONE] host: the drone's energy %.1f -> 100", e);
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if (slot1 && s_step == 0 && t >= 55000u && drone) {
        s_step = 1;
        UE_LOGI("[DRONE] client: pressed the drone's switch (ran=%d)", Press(drone, me, 8) ? 1 : 0);
    }
    if (slot1 && s_step == 1 && t >= 60000u && t < 72000u && t >= s_nextMove) {
        s_nextMove = t + 100u;
        const bool upPhase = t < 66000u;
        CallWith(drone, L"move", L"forward", upPhase ? 0.f : 800.f, L"rot", 0.f, L"up", upPhase ? 800.f : 0.f);
    }
    if (slot1 && s_step == 1 && t >= 80000u) {
        s_step = 2;
        UE_LOGI("[DRONE] client: pressed the drone's switch again (ran=%d)", Press(drone, me, 8) ? 1 : 0);
    }
    if (slot1 && s_step == 2 && t >= 90000u && sled) {
        s_step = 3;
        UE_LOGI("[DRONE] client: sat on the sled (ran=%d)", CallPlayer(sled, L"playerSit", me) ? 1 : 0);
    }
    if (slot1 && s_step == 3 && t >= 91000u) {
        s_step = 4;
        UE_LOGI("[DRONE] client: the sled's forward held (ran=%d), shoved as a slope would (ok=%d)",
                CallPressed(sled, L"moveForward", true) ? 1 : 0,
                ue_wrap::engine::SetComponentLinearVelocity(ue_wrap::engine::RootComponentOf(sled), 600.f, 0.f, 0.f) ? 1 : 0);
    }
    if (slot1 && s_step == 4 && t >= 95000u) {
        s_step = 5;
        CallPressed(sled, L"moveForward", false);
    }
    if (host && s_step == 2 && t >= 110000u && sled) {
        s_step = 3;
        UE_LOGI("[DRONE] host: sat on the sled (ran=%d), forward held (ran=%d)", CallPlayer(sled, L"playerSit", me) ? 1 : 0,
                CallPressed(sled, L"moveForward", true) ? 1 : 0);
    }
    if (host && s_step == 3 && t >= 111000u && sled) {
        s_step = 4;
        UE_LOGI("[DRONE] host: shoved the sled (ok=%d)",
                ue_wrap::engine::SetComponentLinearVelocity(ue_wrap::engine::RootComponentOf(sled), -600.f, 0.f, 0.f) ? 1 : 0);
    }
    if (host && s_step == 4 && t >= 116000u) {
        s_step = 5;
        CallPressed(sled, L"moveForward", false);
        UE_LOGI("[DRONE] host: got off the sled (ran=%d)", CallPlayer(sled, L"playerUnsit", me) ? 1 : 0);
    }
    if (slot1 && s_step == 5 && t >= 96000u) {
        s_step = 6;
        UE_LOGI("[DRONE] client: got off the sled (ran=%d)", CallPlayer(sled, L"playerUnsit", me) ? 1 : 0);
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 140000u || t < s_next) return;
    s_next = t + 2000u;
    ue_wrap::FVector d{}, sl{};
    if (drone) E::TryGetActorLocation(drone, d);
    if (sled) E::TryGetActorLocation(sled, sl);
    UE_LOGI("[DRONE] %s t=%llu: drone at (%.0f,%.0f,%.0f) active=%d energy=%.1f sim=%d; sled at (%.0f,%.0f,%.0f) sim=%d "
            "forward=%d", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u), d.X, d.Y, d.Z,
            drone ? BoolOf(drone, L"active") : -1, FloatOf(drone, L"energy"),
            drone && ue_wrap::engine::IsActorRootSimulatingPhysics(drone) ? 1 : 0, sl.X, sl.Y, sl.Z,
            sled && ue_wrap::engine::IsActorRootSimulatingPhysics(sled) ? 1 : 0, sled ? BoolOf(sled, L"pressedForward") : -1);
    if (t >= 138000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=catalog: HOST, at 20 s, the game's store table (list_store) and prop table (list_props), a row a
// line (`[CATALOG]`, the table, the row, then each column's name and value), every column read through the engine's own
// column getter.
bool Catalog() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "catalog";
    return v;
}

void DumpTable(const wchar_t* name) {
    void* const table = R::FindObject(name, L"DataTable");
    void* const rs = table ? ue_wrap::data_table::RowStruct(table) : nullptr;
    std::vector<ue_wrap::data_table::RowRef> rows;
    if (!rs || !ue_wrap::data_table::Rows(table, rows, "entity_probe")) {
        UE_LOGW("[CATALOG] %ls: not found", name);
        return;
    }
    std::vector<std::wstring> cols;
    std::vector<std::vector<std::wstring>> vals;
    for (const auto& fld : R::EnumerateStructFields(rs)) {
        std::vector<std::wstring> v;
        if (!ue_wrap::data_table::ColumnAsStrings(table, fld.name, v)) continue;
        cols.push_back(fld.name.substr(0, fld.name.find(L'_')));
        vals.push_back(std::move(v));
    }
    UE_LOGI("[CATALOG] %ls: %zu rows, %zu columns (the first column holds %zu strings)", name, rows.size(), cols.size(),
            vals.empty() ? size_t{0} : vals[0].size());
    for (size_t i = 0; i < rows.size(); ++i) {
        std::wstring line = R::ToString(rows[i].key) + L" |";
        for (size_t c = 0; c < cols.size(); ++c) {
            // A column's strings are its rows', in the RowMap's order (no title row on this build).
            const size_t at = i;
            std::wstring v = at < vals[c].size() ? vals[c][at] : L"?";
            if (v.size() > 120) v = v.substr(0, 120);
            line += L" " + cols[c] + L"=" + v;
        }
        UE_LOGI("[CATALOG] %ls %ls", name, line.c_str());
    }
}

// VOTVCOOP_ENTITY_PROBE=scrap: a trash pile's scoop. HOST 40 s: slot 1 moved beside the first actorChipPile_C, whose
// place every peer is told by that pile; CLIENT slot 1, 55 s: the pile nearest its body runs its own turnIntoScrap,
// as the shovel's dig does. Every peer, each 5 s to 120 s (`[SCRAP]`): its piles, and whether one stands at the place.
bool Scrap() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "scrap";
    return v;
}

bool Four() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "four";
    return v;
}

bool Real() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "real";
    return v;
}

// `fn` on the first live `cls`, or on `holder`'s field when `cls` is null.
bool RunOn(const wchar_t* cls, const wchar_t* fn) {
    for (void* o : R::FindObjectsByClass(cls)) {
        if (!o || !R::IsLive(o) || R::NameStartsWith(R::NameOf(o), L"Default__")) continue;
        void* const f = R::FindDispatchFunction(R::ClassOf(o), fn, nullptr);
        ue_wrap::ParamFrame pf(f);
        return pf.valid() && ue_wrap::Call(o, pf);
    }
    return false;
}

int g_realStep = 0;
void RealStep(uint64_t t) {
    if (g_realStep == 0 && t >= 35000u) {
        g_realStep = 1;
        // As the day cycle's superFogEvent does past its 5 % roll: the super fog at the origin, whose own spawnUfo mints
        // the Thunder Wisp.
        const CR::Resolved r = CR::ResolveClass(L"superFog_C", CR::AnyFamily(), CR::Load::Immediate);
        void* const fog = r.state == CR::ClassState::Found && r.cls ? E::SpawnActor(r.cls, ue_wrap::FVector{}) : nullptr;
        UE_LOGI("[ENTITY] host: the super fog spawned as its event spawns it (spawned=%d)", fog ? 1 : 0);
    }
    if (g_realStep == 1 && t >= 75000u) {
        g_realStep = 2;
        UE_LOGI("[ENTITY] host: the signal screen's spawn_trifo (ran=%d)",
                RunOn(L"analogDScreenTest_C", L"spawn_trifo") ? 1 : 0);
    }
}

uint64_t g_startAt = 0, g_nextLog = 0;
size_t g_next = 0;
void* g_spawned = nullptr;
uint64_t g_spawnedAt = 0;

size_t LiveCount(const wchar_t* cls) {
    size_t n = 0;
    for (void* o : R::FindObjectsByClass(cls))
        if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) ++n;
    return n;
}

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return nullptr;
    void* v = nullptr;
    std::memcpy(&v, static_cast<const uint8_t*>(obj) + off, sizeof(v));
    return v && R::IsLive(v) ? v : nullptr;
}

int RunMany(const wchar_t* cls, const wchar_t* fn, int times) {
    int n = 0;
    for (int i = 0; i < times; ++i) n += RunOn(cls, fn) ? 1 : 0;
    return n;
}

void Drive() {
    UE_LOGI("[ANOM] drive: log.Teleport=%d gost.Teleport=%d booth.Timer=%d goat.lookPlace=%d gm.tickerBody=%d "
            "gm.tickerFunguy=%d mimic.gen=%d",
            RunMany(L"log_C", L"Teleport", 1), RunMany(L"gost_C", L"Teleport", 1), RunMany(L"teleBooth_C", L"Timer", 40),
            RunMany(L"goat_C", L"lookPlace", 60), RunMany(L"mainGamemode_C", L"tickerBody", 300),
            RunMany(L"mainGamemode_C", L"tickerFunguy", 300),
            RunMany(L"impostorPropsMaster_C", L"gen", 10));
}

std::wstring Points(const wchar_t* cls) {
    std::wstring out;
    int k = 0;
    for (void* o : R::FindObjectsByClass(cls)) {
        if (!o || !R::IsLive(o) || R::NameStartsWith(R::NameOf(o), L"Default__")) continue;
        ue_wrap::FVector at{};
        if (void* stand = std::wcscmp(cls, L"goat_C") == 0 ? Field(o, L"goatstand") : nullptr)
            at = ue_wrap::engine::GetComponentLocation(stand);
        else E::TryGetActorLocation(o, at);
        if (++k > 4) break;
        out += L"(" + std::to_wstring(static_cast<int>(at.X / 1000.f)) + L"," +
               std::to_wstring(static_cast<int>(at.Y / 1000.f)) + L")";
    }
    return out;
}

// Each class with a live one: its count and its first points; then the owners of each part.
void Census(bool host, uint64_t t) {
    std::wstring line;
    for (size_t i = 0; i < ListCount(); ++i)
        if (const size_t n = LiveCount(List()[i])) line += std::wstring(List()[i]) + L"=" + std::to_wstring(n) + Points(List()[i]) + L" ";
    UE_LOGI("[ANOM] %s t=%llu: %ls", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u), line.c_str());
    for (const wchar_t* part : kParts) {
        std::map<std::wstring, std::wstring> owners;
        for (void* c : R::FindObjectsByClass(part)) {
            if (!c || !R::IsLive(c) || R::NameStartsWith(R::NameOf(c), L"Default__")) continue;
            void* const a = R::OuterOf(c);
            if (!a || !R::IsLive(a)) continue;
            ue_wrap::FVector at{};
            E::TryGetActorLocation(a, at);
            owners[R::ClassNameOf(a)] += L"(" + std::to_wstring(static_cast<int>(at.X / 1000.f)) + L"," +
                                         std::to_wstring(static_cast<int>(at.Y / 1000.f)) + L")";
        }
        std::wstring o;
        for (const auto& kv : owners) o += kv.first + kv.second + L" ";
        UE_LOGI("[ANOM] %s t=%llu %ls: %ls", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u), part,
                o.c_str());
    }
}

void* First(const wchar_t* cls) {
    void* one[1];
    return ue_wrap::world_instances::Find(cls, one, 1) > 0 && R::IsLive(one[0]) ? one[0] : nullptr;
}

bool SetBool(void* obj, const wchar_t* name, bool v) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return false;
    uint8_t* const b = static_cast<uint8_t*>(obj) + off;
    *b = static_cast<uint8_t>(v ? (*b | mask) : (*b & ~mask));
    return true;
}

bool SetFloat(void* obj, const wchar_t* name, float v) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return false;
    std::memcpy(static_cast<uint8_t*>(obj) + off, &v, sizeof(v));
    return true;
}

void* SpawnStamped(const wchar_t* name, const ue_wrap::FVector& at) {
    const CR::Resolved r = CR::ResolveClass(name, CR::AnyFamily(), CR::Load::Immediate);
    if (r.state != CR::ClassState::Found || !r.cls) return nullptr;
    void* const a = E::BeginDeferredSpawn(r.cls, at, ue_wrap::FRotator{});
    if (!a) return nullptr;
    ue_wrap::spawner_stamp::Stamp(r.cls, a);
    return E::FinishDeferredSpawn(a, at, ue_wrap::FRotator{}) ? a : nullptr;
}

void ScrapStep(coop::net::Session& session, bool host, uint64_t t) {
    static int s_step = 0;
    static ue_wrap::FVector s_place{};
    static bool s_havePlace = false;
    static void* piles[16384];
    const int n = ue_wrap::world_instances::Find(L"actorChipPile_C", piles, 16384);
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector meAt{};
    if (me) E::TryGetActorLocation(me, meAt);
    if (host && s_step == 0 && t >= 40000u && n > 0) {
        s_step = 1;
        E::TryGetActorLocation(piles[0], s_place);
        s_havePlace = true;
        UE_LOGI("[SCRAP] host: slot 1 moved beside the pile at (%.0f,%.0f,%.0f) (ok=%d)", s_place.X, s_place.Y, s_place.Z,
                coop::teleport_client::OrderToPose(session, 1, ue_wrap::FVector{s_place.X + 150.f, s_place.Y, s_place.Z + 120.f},
                                                   ue_wrap::FRotator{}) ? 1 : 0);
    }
    if (!host && s_step == 0 && t >= 55000u && coop::players::Registry::Get().LocalPeerId() == 1) {
        s_step = 1;
        void* best = nullptr;
        float bestD = 500.f * 500.f;
        for (int k = 0; k < n; ++k) {
            ue_wrap::FVector at{};
            if (!E::TryGetActorLocation(piles[k], at)) continue;
            const float dx = at.X - meAt.X, dy = at.Y - meAt.Y, dz = at.Z - meAt.Z, d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) {
                bestD = d;
                best = piles[k];
                s_place = at;
            }
        }
        s_havePlace = best != nullptr;
        ue_wrap::ParamFrame pf(best ? R::FindDispatchFunction(R::ClassOf(best), L"turnIntoScrap", nullptr) : nullptr);
        const bool ran = pf.valid() && ue_wrap::Call(best, pf);
        UE_LOGI("[SCRAP] client: the pile at (%.0f,%.0f,%.0f) scooped as the shovel does (found=%d ran=%d)", s_place.X,
                s_place.Y, s_place.Z, best ? 1 : 0, ran ? 1 : 0);
    }
    // A client learns the place from the pile nearest its body once it stands there.
    if (!host && !s_havePlace && t >= 50000u) {
        for (int k = 0; k < n; ++k) {
            ue_wrap::FVector at{};
            if (!E::TryGetActorLocation(piles[k], at)) continue;
            const float dx = at.X - meAt.X, dy = at.Y - meAt.Y;
            if (dx * dx + dy * dy < 400.f * 400.f) {
                s_place = at;
                s_havePlace = true;
            }
        }
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 120000u || t < s_next) return;
    s_next = t + 5000u;
    int atPlace = 0;
    for (int k = 0; k < n && s_havePlace; ++k) {
        ue_wrap::FVector at{};
        if (!E::TryGetActorLocation(piles[k], at)) continue;
        const float dx = at.X - s_place.X, dy = at.Y - s_place.Y, dz = at.Z - s_place.Z;
        atPlace += dx * dx + dy * dy + dz * dz < 150.f * 150.f ? 1 : 0;
    }
    UE_LOGI("[SCRAP] %s t=%llu: piles=%d placeKnown=%d pileAtPlace=%d", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), n, s_havePlace ? 1 : 0, atPlace);
    if (t >= 115000u) UE_LOGI("[ENTITY] done");
}

void FourStep(coop::net::Session& session, bool host, uint64_t t) {
    static int s_step = 0;
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector meAt{};
    ue_wrap::FRotator meRot{};
    if (me) {
        E::TryGetActorLocation(me, meAt);
        E::TryGetActorRotation(me, meRot);
    }
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        ue_wrap::FVector at{};
        void* const stone = First(L"prop_hexpillar_C");
        if (stone && E::TryGetActorLocation(stone, at))
            UE_LOGI("[FOUR] host: slot 1 moved beside the stone (ok=%d)",
                    coop::teleport_client::OrderToPose(session, 1, ue_wrap::FVector{at.X + 300.f, at.Y, at.Z + 150.f},
                                                       ue_wrap::FRotator{}) ? 1 : 0);
    }
    if (slot1 && s_step == 0 && t >= 55000u) {
        s_step = 1;
        UE_LOGI("[FOUR] client: its own digUp of the stone (ran=%d)", RunOn(L"prop_hexpillar_C", L"digUp") ? 1 : 0);
    }
    if (host && s_step == 1 && t >= 70000u) {
        s_step = 2;
        void* const p = First(L"hexpillar_C");
        UE_LOGI("[FOUR] host: the pillar's a set to 0.95 (ok=%d)", p && SetFloat(p, L"a", 0.95f) ? 1 : 0);
    }
    if (host && s_step == 2 && t >= 80000u) {
        s_step = 3;
        const bool ran = RunOn(L"antibreatherSPawner_C", L"cencipede");
        // The spawner's loop ends its Wolfgang while player 0 is far and not looking: let go of her.
        void* const cave = First(L"antibreatherSPawner_C");
        const int32_t off = cave ? R::FindPropertyOffset(R::ClassOf(cave), L"brether") : -1;
        if (off >= 0) std::memset(static_cast<uint8_t*>(cave) + off, 0, sizeof(void*));
        UE_LOGI("[FOUR] host: the cave spawner's cencipede (ran=%d), its hold on her let go (ok=%d)", ran ? 1 : 0,
                off >= 0 ? 1 : 0);
    }
    if (slot1 && s_step == 1 && t >= 90000u) {
        s_step = 2;
        UE_LOGI("[FOUR] client: its own cave spawner's cencipede (ran=%d)",
                RunOn(L"antibreatherSPawner_C", L"cencipede") ? 1 : 0);
    }
    const float yaw = meRot.Yaw * 0.0174532925f;
    const CR::Resolved tree = CR::ResolveClass(L"prop_xmastree_C", CR::AnyFamily(), CR::Load::Immediate);
    if (host && s_step == 3 && t >= 100000u) {
        s_step = 4;
        void* const n = SpawnStamped(L"tomtenisse_C", ue_wrap::FVector{meAt.X + 500.f * std::cos(yaw),
                                                                     meAt.Y + 500.f * std::sin(yaw), meAt.Z});
        UE_LOGI("[FOUR] host: a Nisse (ok=%d) beside the host", n ? 1 : 0);
    }
    // A raw tree is not a keyed prop and lasts seconds: each side stands its own just before the wake.
    if (host && s_step == 4 && t >= 112000u && !First(L"prop_xmastree_C")) {
        void* const tr = tree.cls ? E::SpawnActor(tree.cls, ue_wrap::FVector{meAt.X + 600.f * std::cos(yaw),
                                                                              meAt.Y + 600.f * std::sin(yaw), meAt.Z})
                                  : nullptr;
        UE_LOGI("[FOUR] host: a Christmas tree beside the host (ok=%d)", tr ? 1 : 0);
    }
    if (slot1 && s_step == 2 && t >= 113000u && !First(L"prop_xmastree_C") && tree.cls)
        E::SpawnActor(tree.cls, ue_wrap::FVector{meAt.X + 600.f, meAt.Y, meAt.Z});
    if (slot1 && s_step == 2 && t >= 115000u) {
        s_step = 3;
        void* const tr = First(L"prop_xmastree_C");
        const bool marked = SetBool(tr, L"sleptThroughMidnight", true);
        UE_LOGI("[FOUR] client: its tree's night marked (ok=%d) and its woken (ran=%d)", marked ? 1 : 0,
                RunOn(L"prop_xmastree_C", L"woken") ? 1 : 0);
    }
    if (host && s_step == 4 && t >= 130000u) {
        s_step = 5;
        const ue_wrap::FVector at{meAt.X - 2500.f * std::cos(yaw), meAt.Y - 2500.f * std::sin(yaw), meAt.Z};
        void* const b = SpawnStamped(L"birch_C", at);
        bool set = b != nullptr;
        set = set && SetBool(b, L"angry", true) && SetBool(b, L"active", true) && SetBool(b, L"appeared", true);
        set = set && SetFloat(b, L"lookAwayTime", 5.f);
        const bool moved = b && coop::teleport_client::OrderToPose(
                                    session, 1, ue_wrap::FVector{at.X + 60.f, at.Y, at.Z + 100.f}, ue_wrap::FRotator{});
        UE_LOGI("[FOUR] host: a birch 25 m behind (ok=%d), angry and looked away from (set=%d), slot 1 into it (ok=%d)",
                b ? 1 : 0, set ? 1 : 0, moved ? 1 : 0);
    }
    // The birch's tick spends its anger at once; once slot 1 stands at it, it is angry again, each 10 s.
    static uint64_t s_reArm = 0;
    if (host && s_step == 5 && t >= 140000u && t >= s_reArm && t < 230000u) {
        s_reArm = t + 10000u;
        void* const b = First(L"birch_C");
        ue_wrap::FVector bAt{};
        if (b && E::TryGetActorLocation(b, bAt)) {
            coop::teleport_client::OrderToPose(session, 1, ue_wrap::FVector{bAt.X, bAt.Y + 90.f, bAt.Z + 100.f},
                                               ue_wrap::FRotator{});
            UE_LOGI("[FOUR] host: the birch angry again (ok=%d), slot 1 moved to it", SetBool(b, L"angry", true) ? 1 : 0);
        }
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 300000u || t < s_next) return;
    s_next = t + 10000u;
    static const wchar_t* const kSeen[] = {L"hexpillar_C", L"centipedeHead_C", L"centipedeSegmet_C",
                                           L"tomtenisse_C", L"birch_C",         L"prop_xmastree_C"};
    std::wstring line;
    void* all[512];
    for (const wchar_t* cls : kSeen) {
        const int n = ue_wrap::world_instances::Find(cls, all, 512);
        int mirrors = 0;
        for (int k = 0; k < n; ++k) mirrors += coop::world_actor_sync::IsMirroredActor(all[k]) ? 1 : 0;
        line += std::wstring(cls) + L"=" + std::to_wstring(n) + L"/" + std::to_wstring(mirrors) + L" ";
    }
    UE_LOGI("[FOUR] %s t=%llu: (count/mirrors) %ls", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u),
            line.c_str());
    if (void* const b = host ? First(L"birch_C") : nullptr) {
        auto bit = [&](const wchar_t* n) {
            int32_t off = -1;
            uint8_t mask = 0;
            return R::FindBoolProperty(R::ClassOf(b), n, off, mask) ? ((static_cast<const uint8_t*>(b)[off] & mask) ? 1 : 0) : -1;
        };
        auto flt = [&](const wchar_t* n) {
            const int32_t off = R::FindPropertyOffset(R::ClassOf(b), n);
            float v = -1.f;
            if (off >= 0) std::memcpy(&v, static_cast<const uint8_t*>(b) + off, sizeof(v));
            return v;
        };
        ue_wrap::FVector bAt{}, pAt{};
        E::TryGetActorLocation(b, bAt);
        coop::RemotePlayer* const rp = coop::players::Registry::Get().Puppet(1);
        if (rp && rp->actor()) E::TryGetActorLocation(rp->actor(), pAt);
        const float dx = bAt.X - pAt.X, dy = bAt.Y - pAt.Y, dz = bAt.Z - pAt.Z;
        UE_LOGI("[FOUR] host birch: newvar_1=%d active=%d appeared=%d angry=%d rendered=%d lookAway=%.1f lookAt=%.1f "
                "slot1 at %.0f cm", bit(L"NewVar_1"), bit(L"active"), bit(L"appeared"), bit(L"angry"), bit(L"rendered"),
                flt(L"lookAwayTime"), flt(L"lookAtTime"), std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    if (t >= 290000u) UE_LOGI("[ENTITY] done");
}

void HostStep(uint64_t now, uint64_t t) {
    if (g_spawned && now - g_spawnedAt >= 12000u) {
        const bool live = R::IsLive(g_spawned);
        UE_LOGI("[ENTITY] host: destroy %ls (live=%d ok=%d)", R::ClassNameOf(g_spawned).c_str(), live ? 1 : 0,
                live && E::DestroyActor(g_spawned) ? 1 : 0);
        g_spawned = nullptr;
    }
    const uint64_t first = Anom() ? 80000u : 35000u;
    if (g_next >= ListCount() || t < first + static_cast<uint64_t>(g_next) * 20000u) return;
    const wchar_t* const name = List()[g_next++];
    const CR::Resolved r = CR::ResolveClass(name, CR::AnyFamily(), CR::Load::Immediate);
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector at{};
    ue_wrap::FRotator rot{};
    if (!me || !E::TryGetActorLocation(me, at)) return;
    E::TryGetActorRotation(me, rot);
    const float yaw = rot.Yaw * 0.0174532925f;
    const ue_wrap::FVector to{at.X + 3000.f * std::cos(yaw), at.Y + 3000.f * std::sin(yaw), at.Z + 200.f};
    g_spawned = r.state == CR::ClassState::Found && r.cls ? E::SpawnActor(r.cls, to) : nullptr;
    g_spawnedAt = now;
    UE_LOGI("[ENTITY] host: spawn %ls (class %s, spawned=%d)", name,
            r.state == CR::ClassState::Found ? "found" : r.state == CR::ClassState::Absent ? "absent" : "not ready",
            g_spawned ? 1 : 0);
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!Armed() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const uint64_t now = ::GetTickCount64();
    if (!g_startAt) {
        g_startAt = now;
        UE_LOGI("[ENTITY] probe armed");
    }
    const uint64_t t = now - g_startAt;
    if (host) {
        if (Real()) RealStep(t);
        else HostStep(now, t);
    }
    if (Lamps()) {
        LampsStep(host, t);
        return;
    }
    if (ChurnMode()) {
        ChurnStep(host, t);
        return;
    }
    if (DroneMode()) {
        DroneStep(host, t);
        return;
    }
    if (Catalog()) {
        static bool s_dumped = false;
        if (host && !s_dumped && t >= 20000u) {
            s_dumped = true;
            DumpTable(L"list_store");
            DumpTable(L"list_props");
            UE_LOGI("[ENTITY] done");
        }
        return;
    }
    if (Scrap()) {
        ScrapStep(*session, host, t);
        return;
    }
    if (Four()) {
        FourStep(*session, host, t);
        return;
    }
    if (Anom()) {
        static bool s_driven = false;
        if (!s_driven && t >= 45000u) {
            s_driven = true;
            Drive();
        }
        const uint64_t end = 80000u + ListCount() * 20000u + 20000u;
        if (t >= 30000u && now >= g_nextLog && t <= end) {
            g_nextLog = now + (t < 80000u ? 10000u : 4000u);
            if (t < 80000u) Census(host, t);
            else {
                std::wstring line;
                for (size_t i = 0; i < ListCount(); ++i)
                    if (const size_t n = LiveCount(List()[i])) line += std::wstring(List()[i]) + L"=" + std::to_wstring(n) + L" ";
                UE_LOGI("[ENTITY] %s t=%llu: %ls", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u),
                        line.c_str());
            }
        }
        static bool s_adone = false;
        if (!s_adone && t >= end + 2000u) {
            s_adone = true;
            Census(host, t);
            UE_LOGI("[ENTITY] done");
        }
        return;
    }
    if (t >= 30000u && now >= g_nextLog && t <= 35000u + kCount * 20000u + 20000u) {
        g_nextLog = now + 4000u;
        std::wstring line;
        for (const wchar_t* c : kClasses)
            if (const size_t n = LiveCount(c)) line += std::wstring(c) + L"=" + std::to_wstring(n) + L" ";
        if (const size_t n = LiveCount(L"superFog_C")) line += L"superFog_C=" + std::to_wstring(n) + L" ";
        UE_LOGI("[ENTITY] %s t=%llu: %ls", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u),
                line.c_str());
    }
    static bool s_done = false;
    if (!s_done && t >= 35000u + kCount * 20000u + 22000u) {
        s_done = true;
        UE_LOGI("[ENTITY] done");
    }
}

}  // namespace coop::dev::entity_probe
