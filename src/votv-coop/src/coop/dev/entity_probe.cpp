// coop/dev/entity_probe.cpp -- [dev] the wiki's extraterrestrials on every peer (VOTVCOOP_ENTITY_PROBE=1 on every
// peer). HOST: from 35 s, every 20 s the next class of the list is loaded and spawned 30 m before its player, and
// destroyed 12 s on (`[ENTITY] host: spawn`, `destroy`). Every peer, each 4 s (`[ENTITY]`): its live count of each
// class of the list that has one, so a class the host spawned and a client never counted, or kept, is seen.
// VOTVCOOP_ENTITY_PROBE=real: the game's own spawns instead -- 35 s the super fog as its event spawns it (its own
// spawnUfo mints the Thunder Wisp), 75 s the signal screen's spawn_trifo (the TriFO).

#include "coop/dev/entity_probe.h"

#include "coop/config/config.h"
#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "atv_seat.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/interactables/atv_sync.h"
#include "coop/dev/director/director.h"
#include "coop/dev/drill_hand.h"
#include "coop/props/bucket_pour.h"
#include "coop/props/bucket_sync.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/photo_sync.h"
#include "coop/session/teleport_client.h"
#include "coop/world/world_actor_sync.h"
#include "harness/screenshot.h"
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
#include "ue_wrap/actors/bucket.h"
#include "ue_wrap/actors/prop_use.h"
#include "ue_wrap/engine/engine_physics.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/devices/cremator.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/actors/floppy_disc.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/devices/window_canvas.h"
#include "coop/interactables/window_stroke_sync.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/actors/save_record.h"
#include "ue_wrap/actors/photo_store.h"
#include "ue_wrap/devices/grime.h"
#include "ue_wrap/actors/spawner_stamp.h"
#include "ue_wrap/engine/data_table.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

// VOTVCOOP_ENTITY_PROBE=photo: the camera and the tape. HOST 40 s: a printer camera, a tape and an empty bottle 2.5 m before
// the host; 42 s: the bottle's health to half its full. CLIENT slot 1, 50 s: one paper pocketed; 55 s: the camera's
// click (its own player's view); 70 s: the click again, with no paper. HOST 60 s: a paper pocketed; 65 s: the click; 80 s:
// a photo of its print laid in the world. CLIENT slot 1, 90 s: the tape on the bottle; 95 s: again, at full. Every peer,
// each 5 s to 120 s (`[PHOTO]`): the gallery's and the printed pictures' lengths, the last two pictures' sizes and
// hashes, each photo copy's index with its picture's hash, the bottle's health and the tape's uses. HOST 100 s: photo 0
// (slot 1's print) laid 80 cm before it. Every peer in turn (3 s apart, by slot), once its copy of photo 0 stands, steps
// 1.2 m off its +x side, narrows its view onto it and captures its screen (coop-screenshots/, `photo-host`...).
bool PhotoMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "photo";
    return v;
}

uint32_t Fnv(const std::vector<uint8_t>& b) {
    uint32_t h = 2166136261u;
    for (uint8_t c : b) h = (h ^ c) * 16777619u;
    return h;
}

float* BottleHealth(void* a, float* full) {
    static R::InstanceOffset s_impact(L"physicsImpact");
    const int32_t off = a ? s_impact.Of(a) : -1;
    void* const comp = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(a) + off) : nullptr;
    if (!comp || !R::IsLive(comp)) return nullptr;
    const int32_t h = R::FindPropertyOffset(R::ClassOf(comp), L"health");
    const int32_t d = R::FindPropertyOffset(R::ClassOf(comp), L"damageData");
    void* const st = R::PropertyInnerStruct(R::ClassOf(comp), L"damageData");
    const int32_t f = st ? R::FindPropertyOffsetByPrefix(st, L"health_") : -1;
    if (h < 0 || d < 0 || f < 0) return nullptr;
    if (full) std::memcpy(full, static_cast<char*>(comp) + d + f, sizeof(float));
    return reinterpret_cast<float*>(static_cast<char*>(comp) + h);
}

bool Pocket(void* me, const wchar_t* cls) {
    const CR::Resolved rc = CR::ResolveClass(cls, CR::AnyFamily(), CR::Load::Immediate);
    ue_wrap::FVector at{};
    if (!me || !rc.cls || !E::TryGetActorLocation(me, at)) return false;
    void* const a = E::SpawnActor(rc.cls, at);
    ue_wrap::ParamFrame f(a ? R::FindDispatchFunction(R::ClassOf(me), L"putObjectInventory2", nullptr) : nullptr);
    return f.valid() && f.Set<void*>(L"inputPin", a) && f.Set<bool>(L"noNotify", false) && ue_wrap::Call(me, f);
}

void PhotoStep(bool host, uint64_t t) {
    namespace PS = ue_wrap::photo_store;
    static int s_step = 0;
    static int32_t s_hostPrint = -1;
    static int32_t s_clientPrint = 0;  // HOST: the index slot 1's print was filed at; a client finds it by its picture
    void* const me = coop::players::Registry::Get().Local();
    void* const cam = First(L"prop_digcam_print_C");
    void* const tape = First(L"prop_officetape_C");
    void* const bottle = First(L"prop_emptyBottle_C");
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
        for (const wchar_t* c : {L"prop_digcam_print_C", L"prop_officetape_C", L"prop_emptyBottle_C"}) {
            const CR::Resolved rc = CR::ResolveClass(c, CR::AnyFamily(), CR::Load::Immediate);
            const float side = (static_cast<float>(k++) - 1.f) * 120.f;
            const ue_wrap::FVector to{at.X + 250.f * std::cos(yaw) - side * std::sin(yaw),
                                      at.Y + 250.f * std::sin(yaw) + side * std::cos(yaw), at.Z};
            UE_LOGI("[PHOTO] host: %ls spawned=%d", c, rc.cls && E::SpawnActor(rc.cls, to) ? 1 : 0);
        }
    }
    if (host && s_step == 1 && t >= 42000u && bottle) {
        s_step = 2;
        float full = 0.f;
        float* const h = BottleHealth(bottle, &full);
        if (h) *h = full * 0.5f;
        UE_LOGI("[PHOTO] host: the bottle's health to %.1f of %.1f", h ? *h : -1.f, full);
    }
    if (host && s_step == 2 && t >= 60000u) {
        s_step = 3;
        UE_LOGI("[PHOTO] host: a paper pocketed (ok=%d)", Pocket(me, L"prop_notebook_paper_C") ? 1 : 0);
    }
    if (host && s_step == 3 && t >= 65000u && cam) {
        s_step = 4;
        UE_LOGI("[PHOTO] host: clicked the camera (ran=%d)", CallPlayer(cam, L"playerHandUse_LMB", me) ? 1 : 0);
    }
    if (host && s_step == 4 && t >= 70000u) {
        s_step = 5;
        s_hostPrint = PS::Count(false) - 1;
        UE_LOGI("[PHOTO] host: its print is photo %d", s_hostPrint);
    }
    if (host && s_step == 5 && t >= 80000u) {
        s_step = 6;
        const CR::Resolved rc = CR::ResolveClass(L"prop_photo_C", CR::AnyFamily(), CR::Load::Immediate);
        ue_wrap::FVector at{};
        if (me) E::TryGetActorLocation(me, at);
        at.Z += 50.f;
        void* const a = rc.cls ? E::BeginDeferredSpawn(rc.cls, at, {}) : nullptr;
        static R::InstanceOffset s_idx(L"index");
        const int32_t off = a ? s_idx.Of(a) : -1;
        if (off >= 0) *reinterpret_cast<int32_t*>(static_cast<char*>(a) + off) = s_hostPrint;
        const bool ok = a && E::FinishDeferredSpawn(a, at, {});
        if (ok) {
            void* const f = R::FindDispatchFunction(R::ClassOf(a), L"loadImage", nullptr);
            if (f) R::CallFunction(a, f, nullptr);
        }
        UE_LOGI("[PHOTO] host: photo %d laid in the world (ok=%d)", s_hostPrint, ok ? 1 : 0);
    }
    if (host && s_step == 6 && t >= 100000u) {
        s_step = 7;
        s_clientPrint = coop::photo_sync::LastClientPrint();
        const CR::Resolved rc = CR::ResolveClass(L"prop_photo_C", CR::AnyFamily(), CR::Load::Immediate);
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        if (me) {
            E::TryGetActorLocation(me, at);
            E::TryGetActorRotation(me, rot);
        }
        const float yaw = rot.Yaw * 0.0174532925f;
        at.X += 80.f * std::cos(yaw);
        at.Y += 80.f * std::sin(yaw);
        void* const a = rc.cls ? E::BeginDeferredSpawn(rc.cls, at, {}) : nullptr;
        static R::InstanceOffset s_idx0(L"index");
        const int32_t off = a ? s_idx0.Of(a) : -1;
        if (off >= 0) *reinterpret_cast<int32_t*>(static_cast<char*>(a) + off) = s_clientPrint;
        const bool ok = a && E::FinishDeferredSpawn(a, at, {});
        if (ok) {
            void* const f = R::FindDispatchFunction(R::ClassOf(a), L"loadImage", nullptr);
            if (f) R::CallFunction(a, f, nullptr);
        }
        UE_LOGI("[PHOTO] host: photo %d, slot 1's print, laid before the host for the screenshots (ok=%d)", s_clientPrint,
                ok ? 1 : 0);
    }
    {
        static uint64_t s_seen = 0;
        static int s_shot = 0;
        void* zero = nullptr;
        void* all[16];
        const int found = ue_wrap::world_instances::Find(L"prop_photo_C", all, 16);
        // The screenshots' photo is the newest one laid (the highest element id), once both laid photos stand.
        uint32_t best = 0;
        for (int i = 0; i < found && found >= 2; ++i) {
            const auto eid = coop::element::Registry::Get().EidForActor(all[i]);
            if (eid != coop::element::kInvalidId && static_cast<uint32_t>(eid) > best) {
                best = static_cast<uint32_t>(eid);
                zero = all[i];
            }
        }
        // Host's copy is laid at 100 s; a client's appears with it, whatever that client's own clock reads.
        if (!zero && s_shot == 0) s_seen = 0;  // a copy seen only in passing starts nothing
        if (zero && !s_seen && (!host || t >= 100000u)) s_seen = t;
        const int slot = host ? 0 : coop::players::Registry::Get().LocalPeerId();
        const uint64_t turn = s_seen + static_cast<uint64_t>(slot) * 3000u;  // one peer at a time on the photo's face
        static ue_wrap::FVector s_home{};
        ue_wrap::FVector eye{}, at{}, body{};
        if (zero && s_seen && s_shot == 0 && t >= turn + 1000u && me && E::TryGetActorLocation(zero, at) &&
            E::TryGetActorLocation(me, body)) {
            s_shot = 1;  // 1.2 m off the photo's +x side, the side its picture faces from the host's spot
            s_home = body;
            const ue_wrap::FVector to{at.X + 120.f, at.Y, body.Z};
            UE_LOGI("[PHOTO] %s: stepped beside photo 0 (ok=%d)", host ? "host" : "client",
                    E::TeleportTo(me, to, ue_wrap::FRotator{}) ? 1 : 0);
        }
        static void* s_cam = nullptr;
        static int32_t s_fovOff = -1;
        static float s_fov = 0.f;
        if (zero && s_shot >= 1 && s_shot < 3 && t >= turn + 2000u && me && E::ReadMainPlayerCameraLocation(me, eye) &&
            E::TryGetActorLocation(zero, at)) {
            E::SetControlRotation(E::GetController(me), coop::director::LookAt(eye, at));
            if (s_shot == 1) {
                s_shot = 2;
                static R::InstanceOffset s_camO(L"camera");
                const int32_t co = s_camO.Of(me);
                s_cam = co >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(me) + co) : nullptr;
                s_fovOff = s_cam ? R::FindPropertyOffset(R::ClassOf(s_cam), L"FieldOfView") : -1;
                if (s_fovOff >= 0) std::memcpy(&s_fov, static_cast<char*>(s_cam) + s_fovOff, sizeof(float));
                UE_LOGI("[PHOTO] %s: turned to photo 0 at (%.0f,%.0f,%.0f), %.0f cm off, fov %.0f", host ? "host" : "client",
                        at.X, at.Y, at.Z, std::sqrt((eye.X - at.X) * (eye.X - at.X) + (eye.Y - at.Y) * (eye.Y - at.Y) +
                                                    (eye.Z - at.Z) * (eye.Z - at.Z)), s_fov);
            }
            if (s_fovOff >= 0) {  // a narrow view, so the photo fills the shot
                const float narrow = 16.f;
                std::memcpy(static_cast<char*>(s_cam) + s_fovOff, &narrow, sizeof(float));
            }
            if (t >= turn + 2800u) {
                s_shot = 3;
                const wchar_t* const label = slot == 0 ? L"photo-host" : slot == 1 ? L"photo-slot1" : L"photo-slot2";
                UE_LOGI("[PHOTO] %s: screenshot %ls saved=%d", host ? "host" : "client", label,
                        harness::screenshot::Capture(label) ? 1 : 0);
                if (s_fovOff >= 0) std::memcpy(static_cast<char*>(s_cam) + s_fovOff, &s_fov, sizeof(float));
                const ue_wrap::FVector home{s_home.X - 150.f, s_home.Y - 150.f * static_cast<float>(slot), s_home.Z};
                E::TeleportTo(me, home, ue_wrap::FRotator{});  // out of the next peer's view
            }
        }
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if (slot1 && s_step == 0 && t >= 50000u) {
        s_step = 1;
        UE_LOGI("[PHOTO] client: a paper pocketed (ok=%d)", Pocket(me, L"prop_notebook_paper_C") ? 1 : 0);
    }
    if (slot1 && s_step == 1 && t >= 55000u && cam) {
        s_step = 2;
        UE_LOGI("[PHOTO] client: clicked the camera (ran=%d)", CallPlayer(cam, L"playerHandUse_LMB", me) ? 1 : 0);
    }
    if (slot1 && s_step == 2 && t >= 70000u && cam) {
        s_step = 3;
        UE_LOGI("[PHOTO] client: clicked the camera with no paper (ran=%d)",
                CallPlayer(cam, L"playerHandUse_LMB", me) ? 1 : 0);
    }
    for (int i = 0; i < 2; ++i) {
        const uint64_t at = i ? 95000u : 90000u;
        if (slot1 && s_step == 3 + i && t >= at && tape && bottle) {
            s_step = 4 + i;
            static R::InstanceOffset s_prop(L"prop");
            const int32_t off = s_prop.Of(tape);
            if (off >= 0) *reinterpret_cast<void**>(static_cast<char*>(tape) + off) = bottle;
            UE_LOGI("[PHOTO] client: taped the bottle (ran=%d)", CallPlayer(tape, L"playerHandUse_LMB", me) ? 1 : 0);
        }
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 125000u || t < s_next) return;
    s_next = t + 5000u;
    const int32_t gal = PS::Count(false), phys = PS::Count(true);
    std::vector<uint8_t> b;
    char last[160] = "";
    int len = 0;
    for (int32_t i = phys - 2 > 0 ? phys - 2 : 0; i < phys; ++i) {
        const bool ok = PS::Read(true, i, b);
        len += std::snprintf(last + len, sizeof(last) - static_cast<size_t>(len), " [%d] %zu/%08x", i, ok ? b.size() : 0,
                             ok ? Fnv(b) : 0u);
    }
    void* photos[16];
    const int n = ue_wrap::world_instances::Find(L"prop_photo_C", photos, 16);
    char copies[160] = "";
    len = 0;
    static R::InstanceOffset s_idx(L"index");
    for (int i = 0; i < n && len < 120; ++i) {
        const int32_t off = s_idx.Of(photos[i]);
        const int32_t idx = off >= 0 ? *reinterpret_cast<const int32_t*>(static_cast<const char*>(photos[i]) + off) : -1;
        const bool ok = PS::Read(true, idx, b);
        len += std::snprintf(copies + len, sizeof(copies) - static_cast<size_t>(len), " %d:%08x", idx, ok ? Fnv(b) : 0u);
    }
    float full = 0.f;
    const float* const bh = BottleHealth(bottle, &full);
    static R::InstanceOffset s_uses(L"uses");
    const int32_t uo = tape ? s_uses.Of(tape) : -1;
    UE_LOGI("[PHOTO] %s t=%llu: gallery %d, pictures %d:%s; photo copies %d:%s; bottle %.1f/%.1f; tape uses %d",
            host ? "host" : "client", static_cast<unsigned long long>(t / 1000u), gal, phys, last, n, copies,
            bh ? *bh : -1.f, full, uo >= 0 ? *reinterpret_cast<const int32_t*>(static_cast<const char*>(tape) + uo) : -1);
    if (t >= 118000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=pour: a client's tipped bucket over a stain. Every peer, 35 s: the cleanable decal nearest the
// host's spot with a process of 50 or more (each peer finds the same one). HOST 40 s: a water bucket 1.2 m off, filled.
// CLIENT slot 1, 50 s: to 1.6 m off the bucket, 52 s on: its grab (the director's aimed grab), then 6 s of its view on the
// decal and the bucket held turned over 60 cm above it, then let go. Every peer, each 2 s to 120 s (`[POUR]`): the decal's process, the
// bucket's water, the pour blobs standing and the lane's counts.
bool PourMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "pour";
    return v;
}

constexpr const wchar_t* kStains[] = {L"grime_dusty_C", L"grime_blood_C", L"grime_blood2_C",
                                      L"grime_blood2_drip_C", L"grime_coffee_C", L"grime_grainy_C", L"grime_oil_C",
                                      L"grime_poo_C",         L"grime_beer_C"};

// The cleanable decal nearest `at` with a process of at least `min` (nullptr when none within 8 m).
void* NearestStain(const ue_wrap::FVector& at, float min) {
    void* best = nullptr;
    float bestD = 800.f * 800.f;
    void* found[64];
    for (const wchar_t* c : kStains) {
        const int n = ue_wrap::world_instances::Find(c, found, 64);
        for (int i = 0; i < n; ++i) {
            float process = 0.f;
            bool cleanable = false;
            ue_wrap::FVector p{};
            if (!ue_wrap::grime::ReadProcess(found[i], process) || process < min ||
                !ue_wrap::grime::ReadIsCleanable(found[i], cleanable) || !cleanable || !E::TryGetActorLocation(found[i], p))
                continue;
            const float d = (p.X - at.X) * (p.X - at.X) + (p.Y - at.Y) * (p.Y - at.Y) + (p.Z - at.Z) * (p.Z - at.Z);
            if (d < bestD) {
                bestD = d;
                best = found[i];
            }
        }
    }
    return best;
}

void PourStep(bool host, uint64_t t) {
    static int s_step = 0;
    static ue_wrap::FVector s_stain{};
    static bool s_have = false;
    static std::unique_ptr<coop::director::AimedGrab> s_grab;
    static uint64_t s_grabbedAt = 0;
    void* const me = coop::players::Registry::Get().Local();
    void* const bucket = First(L"prop_bucket_C");
    if (!s_have && t >= 35000u) {
        s_have = true;  // the host's spot: every peer joined at the host and the decals are the save's
        const ue_wrap::FVector spot{-1044.f, 576.f, 6651.f};
        void* const d = NearestStain(spot, 50.f);
        float process = -1.f;
        if (d && E::TryGetActorLocation(d, s_stain)) ue_wrap::grime::ReadProcess(d, process);
        else s_have = false;
        ue_wrap::FVector centre{};
        float half = -1.f;
        if (d) ue_wrap::grime::ReadBox(d, centre, half);
        UE_LOGI("[POUR] %s: the stain %ls at (%.0f,%.0f,%.0f), process %.1f, its box %.0f cm across about (%.0f,%.0f,%.0f)",
                host ? "host" : "client", d ? R::ClassNameOf(d).c_str() : L"(none)", s_stain.X, s_stain.Y, s_stain.Z,
                process, 2.f * half, centre.X, centre.Y, centre.Z);
    }
    if (host && s_step == 0 && t >= 40000u && s_have) {
        s_step = 1;
        const CR::Resolved rc = CR::ResolveClass(L"prop_bucket_C", CR::AnyFamily(), CR::Load::Immediate);
        const ue_wrap::FVector to{s_stain.X + 120.f, s_stain.Y, s_stain.Z + 15.f};  // beside it: a landing splash cleans
        UE_LOGI("[POUR] host: the bucket spawned=%d", rc.cls && E::SpawnActor(rc.cls, to) ? 1 : 0);
    }
    if (host && s_step == 1 && t >= 42000u && bucket) {
        s_step = 2;
        ue_wrap::bucket::Plain w;
        if (ue_wrap::bucket::ReadPlain(bucket, w)) {
            ue_wrap::bucket::WritePlain(bucket, w.maxHeight, 0, 0.f, false, false);
            static R::InstanceOffset s_interp(L"height_interp");  // eased to the same, so the tick sees no change to pour
            const int32_t off = s_interp.Of(bucket);
            if (off >= 0) *reinterpret_cast<float*>(static_cast<char*>(bucket) + off) = w.maxHeight;
        }
        coop::bucket_sync::MarkDirty(bucket);
        UE_LOGI("[POUR] host: the bucket filled to %.1f", w.maxHeight);
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if (slot1 && s_step == 0 && t >= 50000u && bucket && me) {
        s_step = 1;
        ue_wrap::FVector b{}, body{};
        E::TryGetActorLocation(bucket, b);
        E::TryGetActorLocation(me, body);
        const ue_wrap::FVector to{b.X + 160.f, b.Y, body.Z};
        UE_LOGI("[POUR] client: stepped 1.6 m off the bucket (ok=%d)", E::TeleportTo(me, to, ue_wrap::FRotator{}) ? 1 : 0);
    }
    if (slot1 && s_step == 1 && t >= 52000u && bucket && me) {
        if (!s_grab) s_grab = std::make_unique<coop::director::AimedGrab>(me, bucket);
        const coop::director::GrabState gs = s_grab->Tick();
        if (gs != coop::director::GrabState::Working) {
            s_step = gs == coop::director::GrabState::Grabbed ? 2 : 9;
            s_grabbedAt = t;
            ue_wrap::FVector b{}, body{};
            E::TryGetActorLocation(bucket, b);
            E::TryGetActorLocation(me, body);
            UE_LOGI("[POUR] client: the grab %s (%s); bucket at (%.0f,%.0f,%.0f), body at (%.0f,%.0f,%.0f)",
                    gs == coop::director::GrabState::Grabbed ? "took the bucket" : "failed",
                    gs == coop::director::GrabState::Grabbed ? "" : s_grab->Why(), b.X, b.Y, b.Z, body.X, body.Y,
                    body.Z);
        }
    }
    if (slot1 && s_step == 2 && bucket && me) {
        ue_wrap::FVector eye{};
        if (E::ReadMainPlayerCameraLocation(me, eye))
            E::SetControlRotation(E::GetController(me), coop::director::LookAt(eye, s_stain));
        if (t >= s_grabbedAt + 1500u) {
            ue_wrap::FRotator over{};
            over.Roll = 160.f;  // turned over over the stain: the rim below the base
            E::SetActorLocation(bucket, ue_wrap::FVector{s_stain.X, s_stain.Y, s_stain.Z + 60.f});
            E::SetActorRotation(bucket, over);
        }
        if (t >= s_grabbedAt + 7500u) {
            s_step = 3;
            coop::director::CallOnPlayer(me, L"dropGrabObject");
            UE_LOGI("[POUR] client: let the bucket go");
        }
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 125000u || t < s_next) return;
    s_next = t + 2000u;
    float process = -1.f;
    if (s_have) {
        void* const d = NearestStain(s_stain, 0.f);
        ue_wrap::FVector p{};
        if (d && E::TryGetActorLocation(d, p) && std::fabs(p.X - s_stain.X) < 5.f && std::fabs(p.Y - s_stain.Y) < 5.f)
            ue_wrap::grime::ReadProcess(d, process);
    }
    ue_wrap::bucket::Plain w;
    const bool hasW = bucket && ue_wrap::bucket::ReadPlain(bucket, w);
    void* blobs[64];
    const int nb = ue_wrap::world_instances::Find(L"prop_sponge_bucketPour_C", blobs, 64);
    const coop::bucket_pour::Counts c = coop::bucket_pour::GetCounts();
    UE_LOGI("[POUR] %s t=%llu: stain process %.1f; bucket water %.1f; blobs standing %d; asked %u spawned %u refused %u",
            host ? "host" : "client", static_cast<unsigned long long>(t / 1000u), process, hasW ? w.height : -1.f, nb,
            c.asked, c.spawned, c.refused);
    if (t >= 118000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=atv: a remote driver's body on the ATV. HOST 40 s: an ATV 4 m before the host. CLIENT slot 1,
// 55 s on: sits in it (the game's own seat verb), 10 s on its throttle down 4 s, 20 s on gets off. The host and slot 2, once
// slot 1 is seated there: each logs slot 1's
// body against the seat and `sitarrow`, looks at the seat from where it stands and captures
// its screen (coop-screenshots/, `atv-host`, `atv-slot2`).
bool AtvMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "atv";
    return v;
}

void AtvStep(bool host, uint64_t t) {
    static int s_step = 0;
    static coop::dev::atv_seat::SitTry s_try;
    static uint64_t s_seatedAt = 0;
    static ue_wrap::FVector s_look{};
    void* const me = coop::players::Registry::Get().Local();
    void* const atv = First(L"ATV_C");
    if (host && s_step == 0 && t >= 40000u && me) {
        s_step = 1;
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        E::TryGetActorLocation(me, at);
        E::TryGetActorRotation(me, rot);
        const float yaw = rot.Yaw * 0.0174532925f;
        const CR::Resolved rc = CR::ResolveClass(L"ATV_C", CR::AnyFamily(), CR::Load::Immediate);
        const ue_wrap::FVector to{at.X + 400.f * std::cos(yaw), at.Y + 400.f * std::sin(yaw), at.Z};
        UE_LOGI("[ATV] host: an ATV spawned=%d", rc.cls && E::SpawnActor(rc.cls, to) ? 1 : 0);
    }
    const uint8_t mySlot = host ? 0 : static_cast<uint8_t>(coop::players::Registry::Get().LocalPeerId());
    static uint64_t s_satAt = 0;
    if (mySlot == 1 && s_step < 2 && t >= 55000u && atv && me) {
        s_step = 1;
        if (coop::dev::atv_seat::TrySit(me, atv, s_try)) {
            s_step = 2;
            s_satAt = t;
            UE_LOGI("[ATV] client: the seat verb finished (seated=%d)",
                    coop::atv_sync::ActorSeatedBy(1) == atv ? 1 : 0);
        }
    }
    // The driver's throttle held 4 s from 10 s after the seat, then up; 20 s after it the driver gets off.
    if (mySlot == 1 && atv && s_satAt) {
        if (s_step == 2 && t >= s_satAt + 10000u) {
            s_step = 3;
            UE_LOGI("[ATV] client: throttle down (ran=%d)", ue_wrap::atv::ReplayThrottle(atv, true) ? 1 : 0);
        }
        if (s_step == 3 && t >= s_satAt + 14000u) {
            s_step = 4;
            UE_LOGI("[ATV] client: throttle up (ran=%d)", ue_wrap::atv::ReplayThrottle(atv, false) ? 1 : 0);
        }
        if (s_step == 4 && t >= s_satAt + 20000u) {
            s_step = 5;
            UE_LOGI("[ATV] client: got off (ran=%d)", ue_wrap::atv::Dismount(atv) ? 1 : 0);
        }
    }
    if (mySlot != 1 && atv && me && coop::atv_sync::ActorSeatedBy(1) == atv) {
        if (!s_seatedAt) s_seatedAt = t;
        coop::RemotePlayer* const rp = coop::players::Registry::Get().Puppet(1);
        void* const body = rp ? rp->actor() : nullptr;
        ue_wrap::FVector root{}, b{}, seat{}, sit{};
        ue_wrap::FRotator rot{}, seatRot{};
        if (t >= s_seatedAt + 2000u && s_step < 10 && body && ue_wrap::atv::GetRootTransform(atv, root, rot) &&
            E::TryGetActorLocation(body, b) && ue_wrap::atv::GetSeatTransform(atv, seat, seatRot) &&
            ue_wrap::atv::GetSitArrowLocation(atv, sit)) {
            s_step = 10;
            UE_LOGI("[ATV] %s: slot 1's body (%.0f,%.0f,%.0f), the seat (%.0f,%.0f,%.0f), sitarrow (%.0f,%.0f,%.0f); body - "
                    "seat (%.0f,%.0f,%.0f), body above sitarrow %.0f cm",
                    host ? "host" : "client", b.X, b.Y, b.Z, seat.X, seat.Y, seat.Z, sit.X, sit.Y, sit.Z, b.X - seat.X,
                    b.Y - seat.Y, b.Z - seat.Z, b.Z - sit.Z);
            s_look = ue_wrap::FVector{seat.X, seat.Y, seat.Z + 30.f};
        }
        ue_wrap::FVector eye{};
        if (s_step == 10 && t >= s_seatedAt + 3500u && E::ReadMainPlayerCameraLocation(me, eye)) {
            E::SetControlRotation(E::GetController(me), coop::director::LookAt(eye, s_look));
            if (t >= s_seatedAt + 5000u) {
                s_step = 11;
                const wchar_t* const label = host ? L"atv-host" : L"atv-slot2";
                UE_LOGI("[ATV] %s: screenshot %ls saved=%d", host ? "host" : "client", label,
                        harness::screenshot::Capture(label) ? 1 : 0);
            }
        }
    }
    if (host && t >= 140000u && s_step != 99) {
        s_step = 99;
        UE_LOGI("[ENTITY] done");
    }
}

// VOTVCOOP_ENTITY_PROBE=gas: the cremator's valve and a gas can's fuel. HOST 40 s: the cremator's fuel to 60 and an empty
// gas can at its nozzle (`gascanAlign1`). CLIENT slot 1, 55 s: its copy's valve pressed (the host's reach check off) (lookingAt_valve set, action 4);
// 75 s: pressed again; 85 s: 5 of its can copy's fuel spent. Every peer, each 2 s to 120 s (`[GAS]`): the cremator's fuel
// and valve and the can's fuel.
bool GasMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "gas";
    return v;
}

bool PressValve(void* c, void* me) {
    for (const auto& f : {std::make_pair(L"lookingAt_valve", true), std::make_pair(L"lookingAt_handle", false)}) {
        int32_t off = -1;
        uint8_t mask = 0;
        if (!R::FindBoolProperty(R::ClassOf(c), f.first, off, mask) || off < 0) return false;
        uint8_t& b = *(static_cast<uint8_t*>(c) + off);
        b = f.second ? static_cast<uint8_t>(b | mask) : static_cast<uint8_t>(b & ~mask);
    }
    return Press(c, me, ue_wrap::cremator::kPressAction);
}

void GasStep(bool host, uint64_t t) {
    static int s_step = 0;
    void* const me = coop::players::Registry::Get().Local();
    void* const c = First(L"cremator_C");
    static R::InstanceOffset s_amount(L"amount");
    static R::InstanceOffset s_align(L"gascanAlign1");
    ue_wrap::FVector nozzle{};
    {
        const int32_t off = c ? s_align.Of(c) : -1;
        void* const comp = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(c) + off) : nullptr;
        if (comp) E::TryGetComponentLocation(comp, nozzle);
    }
    void* can = nullptr;  // the can nearest the nozzle
    {
        void* all[64];
        const int n = ue_wrap::world_instances::Find(L"prop_gascan_C", all, 64);
        float best = 300.f * 300.f;
        for (int i = 0; i < n; ++i) {
            ue_wrap::FVector a{};
            if (!E::TryGetActorLocation(all[i], a)) continue;
            const float d = (a.X - nozzle.X) * (a.X - nozzle.X) + (a.Y - nozzle.Y) * (a.Y - nozzle.Y) +
                            (a.Z - nozzle.Z) * (a.Z - nozzle.Z);
            if (d < best) {
                best = d;
                can = all[i];
            }
        }
    }
    auto amount = [&](void* a) -> float* {
        const int32_t off = a ? s_amount.Of(a) : -1;
        return off >= 0 ? reinterpret_cast<float*>(static_cast<char*>(a) + off) : nullptr;
    };
    if (host && s_step == 0 && t >= 40000u && c) {
        s_step = 1;
        ue_wrap::cremator::WriteGasAmount(c, 60.f);
        ue_wrap::FVector at = nozzle;
        const bool where = nozzle.X != 0.f || nozzle.Y != 0.f;
        at.Z += 5.f;
        const CR::Resolved rc = CR::ResolveClass(L"prop_gascan_C", CR::AnyFamily(), CR::Load::Immediate);
        void* const a = where && rc.cls ? E::BeginDeferredSpawn(rc.cls, at, {}) : nullptr;
        if (float* const am = amount(a)) *am = 0.f;
        const bool ok = a && E::FinishDeferredSpawn(a, at, {});
        UE_LOGI("[GAS] host: the cremator's fuel to 60; an empty can at its nozzle (%.0f,%.0f,%.0f) spawned=%d", at.X,
                at.Y, at.Z, ok ? 1 : 0);
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if (slot1 && c && me) {
        if (s_step == 0 && t >= 55000u) {
            s_step = 1;
            UE_LOGI("[GAS] client: pressed the cremator's valve (ran=%d)", PressValve(c, me) ? 1 : 0);
        }
        if (s_step == 1 && t >= 75000u) {
            s_step = 2;
            UE_LOGI("[GAS] client: pressed the cremator's valve again (ran=%d)", PressValve(c, me) ? 1 : 0);
        }
        if (s_step == 2 && t >= 85000u && can) {
            s_step = 3;
            float* const am = amount(can);
            if (am) *am = *am > 5.f ? *am - 5.f : 0.f;
            UE_LOGI("[GAS] client: spent 5 of its can copy's fuel (%.1f left)", am ? *am : -1.f);
        }
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 125000u || t < s_next) return;
    s_next = t + 2000u;
    float gas = -1.f;
    bool pouring = false;
    if (c) {
        ue_wrap::cremator::ReadGasAmount(c, gas);
        ue_wrap::cremator::ReadPouring(c, pouring);
    }
    const float* const am = amount(can);
    UE_LOGI("[GAS] %s t=%llu: cremator fuel %.1f valve %s; can fuel %.1f", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), gas, pouring ? "open" : "shut", am ? *am : -1.f);
    if (t >= 118000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=picture: a custom picture's pick on a frame, a held poster and a wall poster. HOST 40 s: a
// frame (prop_picture_h_C), a held custom poster (prop_poster_C_C) and a wall poster (poster_C, custom) before it.
// CLIENT slot 1, 55, 60 and 65 s: picks, on its copies nearest the host, the gamemode's first frame picture, first poster
// and second poster (each piece's intComs_texturePickerApply, as the picker's apply calls it). HOST 80 s: picks the
// second frame picture on its frame. Every peer, each 5 s to 140 s (`[PIC]`): the three pieces' file names.
bool PicMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "picture";
    return v;
}

void* PicNearest(const wchar_t* cls, void* ref) {
    static void* found[64];
    ue_wrap::FVector at{}, b{};
    if (!ref || !E::TryGetActorLocation(ref, at)) return nullptr;
    const int n = ue_wrap::world_instances::Find(cls, found, 64);
    void* best = nullptr;
    float bestD = 1e18f;
    for (int i = 0; i < n; ++i) {
        if (!R::IsLive(found[i]) || !E::TryGetActorLocation(found[i], b)) continue;
        const float d = (b.X - at.X) * (b.X - at.X) + (b.Y - at.Y) * (b.Y - at.Y) + (b.Z - at.Z) * (b.Z - at.Z);
        if (d < bestD) {
            bestD = d;
            best = found[i];
        }
    }
    return best;
}

// The gamemode's `names`[i] and `texs`[i] (its picker lists), handed to the piece's apply as the picker does.
bool PicPick(void* piece, const wchar_t* names, const wchar_t* texs, int i, std::wstring& picked) {
    static R::InstanceOffset s_gm(L"gamemode");
    const int32_t go = piece ? s_gm.Of(piece) : -1;
    void* const gm = go >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(piece) + go) : nullptr;
    if (!gm || !R::IsLive(gm)) return false;
    const int32_t no = R::FindPropertyOffset(R::ClassOf(gm), names), to = R::FindPropertyOffset(R::ClassOf(gm), texs);
    if (no < 0 || to < 0) return false;
    struct Arr {
        void* data;
        int32_t num, max;
    };
    const Arr* const na = reinterpret_cast<const Arr*>(static_cast<char*>(gm) + no);
    const Arr* const ta = reinterpret_cast<const Arr*>(static_cast<char*>(gm) + to);
    if (i >= na->num || i >= ta->num) return false;
    const R::FName name = static_cast<const R::FName*>(na->data)[i];
    void* const tex = static_cast<void* const*>(ta->data)[i];
    picked = R::ToString(name);
    ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(piece), L"intComs_texturePickerApply"));
    if (!f.valid()) return false;
    f.Set<void*>(L"texture", tex);
    f.Set<int32_t>(L"index", i);
    f.Set<R::FName>(L"pickedfilename", name);
    return ue_wrap::Call(piece, f);
}

std::wstring PicName(void* piece) {
    static R::InstanceOffset s_fn(L"filename");
    const int32_t off = piece ? s_fn.Of(piece) : -1;
    return off >= 0 ? R::ToString(*reinterpret_cast<R::FName*>(static_cast<char*>(piece) + off)) : L"-";
}

void PicStep(bool host, uint64_t t) {
    static int s_step = 0;
    void* const me = coop::players::Registry::Get().Local();
    coop::RemotePlayer* const hp = host ? nullptr : coop::players::Registry::Get().Puppet(0);
    void* const ref = host ? me : (hp ? hp->actor() : nullptr);
    void* const frame = PicNearest(L"prop_picture_h_C", ref);
    void* const held = PicNearest(L"prop_poster_C_C", ref);
    void* const wall = PicNearest(L"poster_C", ref);
    if (host && s_step == 0 && t >= 40000u && me) {
        s_step = 1;
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        E::TryGetActorLocation(me, at);
        E::TryGetActorRotation(me, rot);
        const float yaw = rot.Yaw * 0.0174532925f;
        int k = 0;
        for (const wchar_t* c : {L"prop_picture_h_C", L"prop_poster_C_C", L"poster_C"}) {
            const CR::Resolved rc = CR::ResolveClass(c, CR::AnyFamily(), CR::Load::Immediate);
            const float side = (static_cast<float>(k++) - 1.f) * 120.f;
            const ue_wrap::FVector to{at.X + 250.f * std::cos(yaw) - side * std::sin(yaw),
                                      at.Y + 250.f * std::sin(yaw) + side * std::cos(yaw), at.Z};
            void* const a = rc.cls ? E::BeginDeferredSpawn(rc.cls, to, {}) : nullptr;
            int32_t off = -1;
            uint8_t mask = 0;
            if (a && R::FindBoolProperty(R::ClassOf(a), L"custom", off, mask) && off >= 0)
                *(static_cast<uint8_t*>(a) + off) |= mask;
            UE_LOGI("[PIC] host: %ls spawned=%d", c, a && E::FinishDeferredSpawn(a, to, {}) ? 1 : 0);
        }
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    std::wstring picked;
    if (slot1 && s_step == 0 && t >= 55000u && frame) {
        s_step = 1;
        const bool ran = PicPick(frame, L"filenames_picsh", L"pics_h", 0, picked);
        UE_LOGI("[PIC] client: picked '%ls' for its frame (ran=%d)", picked.c_str(), ran ? 1 : 0);
    }
    if (slot1 && s_step == 1 && t >= 60000u && held) {
        s_step = 2;
        const bool ran = PicPick(held, L"filenames_posters", L"posters", 0, picked);
        UE_LOGI("[PIC] client: picked '%ls' for its held poster (ran=%d)", picked.c_str(), ran ? 1 : 0);
    }
    if (slot1 && s_step == 2 && t >= 65000u && wall) {
        s_step = 3;
        const bool ran = PicPick(wall, L"filenames_posters", L"posters", 1, picked);
        UE_LOGI("[PIC] client: picked '%ls' for its wall poster (ran=%d)", picked.c_str(), ran ? 1 : 0);
    }
    if (host && s_step == 1 && t >= 80000u && frame) {
        s_step = 2;
        const bool ran = PicPick(frame, L"filenames_picsh", L"pics_h", 1, picked);
        UE_LOGI("[PIC] host: picked '%ls' for its frame (ran=%d)", picked.c_str(), ran ? 1 : 0);
    }
    static uint64_t s_next = 0;
    if (t < 30000u || t > 145000u || t < s_next) return;
    s_next = t + 5000u;
    UE_LOGI("[PIC] %s t=%llu: frame '%ls', held poster '%ls', wall poster '%ls'", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), PicName(frame).c_str(), PicName(held).c_str(),
            PicName(wall).c_str());
    if (t >= 140000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=zipcheck: HOST 30 s: each floppy class's default readWrites, and a spawned one's record run
// through the bound a client's put of it meets (floppy_disc::DiscLeafOk against prop_floppyDisc_C's default).
bool ZipMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "zipcheck";
    return v;
}

void ZipStep(bool host, uint64_t t) {
    static bool s_done = false;
    if (!host || s_done || t < 30000u) return;
    s_done = true;
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector at{};
    if (me) E::TryGetActorLocation(me, at);
    at.X += 200.f;
    int32_t base = -1;
    ue_wrap::floppy_disc::ReadDefaultReadWrites(ue_wrap::object_index::ClassByName(L"prop_floppyDisc_C"), base);
    for (const wchar_t* c : {L"prop_floppyDisc_C", L"prop_floppyDisc_Wh_C", L"prop_floppyDisc_R_C", L"prop_floppyDisc_B_C",
                             L"prop_floppyDisc_Bl_C", L"prop_floppyDisc_G_C", L"prop_floppyDisc_O_C",
                             L"prop_floppyDisc_Y_C"}) {
        void* const cdo = R::FindClassDefaultObject(c);
        const int32_t off = cdo ? R::FindPropertyOffset(R::ClassOf(cdo), L"readWrites") : -1;
        const int32_t def = off >= 0 ? *reinterpret_cast<const int32_t*>(static_cast<const char*>(cdo) + off) : -1;
        const CR::Resolved rc = CR::ResolveClass(c, CR::AnyFamily(), CR::Load::Immediate);
        void* const a = rc.cls ? E::SpawnActor(rc.cls, at) : nullptr;
        ue_wrap::save_record::SaveRecord rec;
        const bool cap = a && ue_wrap::save_record::CaptureRecord(a, rec);
        const char* why = "";
        int32_t own = -1;
        ue_wrap::floppy_disc::ReadDefaultReadWrites(rc.cls, own);
        const bool ok = cap && ue_wrap::floppy_disc::DiscLeafOk(rec, own, &why);
        UE_LOGI("[ZIP] %ls: default readWrites %d (bound %d, the old bound %d); disc class %d; record captured %d, ints %d, readWrites %d, "
                "strings %d; a client's put passes %d (%s)",
                c, def, own, base, rc.cls && ue_wrap::floppy_disc::IsDiscClass(rc.cls) ? 1 : 0, cap ? 1 : 0,
                static_cast<int>(rec.ints.size()), !rec.ints.empty() && !rec.ints[0].empty() ? rec.ints[0][0] : -99,
                static_cast<int>(rec.strings.size()), ok ? 1 : 0, ok ? "ok" : why);
        at.Y += 40.f;
    }
    UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=grime: the bay window's dirt. HOST 40 s: its window's dirtify run six times. CLIENT slot 1,
// 50 s: its own dirtify run (refused). HOST 65 s: a sponge no one holds spawned; 70 s: the window's own cleanAtPoint
// with it, five dabs at the window's middle, which the stroke lane cannot send (as a thrown sponge's). Every peer, each
// 10 s from 35 s: its window's picture's size and hash, the picture written beside the log (`grime-<slot>.jpg`).
bool GrimeMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "grime";
    return v;
}

uint32_t GrimeFnv(const std::vector<uint8_t>& b) {
    uint32_t h = 2166136261u;
    for (uint8_t c : b) h = (h ^ c) * 16777619u;
    return h;
}

void GrimeStep(bool host, uint64_t t) {
    static int s_step = 0;
    static uint64_t s_next = 35000;
    void* const me = coop::players::Registry::Get().Local();
    void* const win = coop::window_stroke_sync::Window();
    const uint8_t slot = host ? 0 : static_cast<uint8_t>(coop::players::Registry::Get().LocalPeerId());
    auto call = [&](const wchar_t* fn) {
        ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(win), fn));
        return f.valid() && ue_wrap::Call(win, f);
    };
    if (!win) return;
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        int ran = 0;
        for (int i = 0; i < 6; ++i) ran += call(L"dirtify") ? 1 : 0;
        UE_LOGI("[GRIME] host: its window's dirtify run %d times", ran);
    }
    if (slot == 1 && s_step == 0 && t >= 50000u) {
        s_step = 1;
        UE_LOGI("[GRIME] client: its window's dirtify run (ran=%d)", call(L"dirtify") ? 1 : 0);
    }
    static void* s_sponge = nullptr;
    if (host && s_step == 1 && t >= 65000u) {
        s_step = 2;
        ue_wrap::FVector at{};
        E::TryGetActorLocation(win, at);
        at.Z -= 300.f;
        const CR::Resolved rc = CR::ResolveClass(L"prop_sponge_C", CR::AnyFamily(), CR::Load::Immediate);
        s_sponge = rc.cls ? E::SpawnActor(rc.cls, at) : nullptr;
        UE_LOGI("[GRIME] host: a sponge spawned=%d", s_sponge ? 1 : 0);
    }
    if (host && s_step == 2 && t >= 70000u && s_sponge && R::IsLive(s_sponge)) {
        s_step = 3;
        ue_wrap::FVector at{};
        E::TryGetActorLocation(win, at);
        void* const fn = R::FindDispatchFunctionCached(R::ClassOf(win), L"cleanAtPoint");
        const int32_t pw = R::FindPropertyOffset(R::ClassOf(s_sponge), L"power");
        int ran = 0;
        for (int i = 0; i < 5; ++i) {
            if (pw >= 0) *reinterpret_cast<float*>(static_cast<char*>(s_sponge) + pw) = 100.f;
            ue_wrap::ParamFrame f(fn);
            f.Set<void*>(L"sponge", s_sponge);
            f.Set<ue_wrap::FVector>(L"location", ue_wrap::FVector{at.X + 40.f * i, at.Y, at.Z});
            ran += f.valid() && ue_wrap::Call(win, f) ? 1 : 0;
        }
        UE_LOGI("[GRIME] host: the window's cleanAtPoint run %d times with an unheld sponge", ran);
    }
    if (t < s_next || t > 150000u) return;
    s_next = t + 10000u;
    std::vector<uint8_t> pic;
    const bool ok = ue_wrap::window_canvas::ReadImage(win, pic);
    UE_LOGI("[GRIME] %s t=%llu: its window's picture read=%d, %zu bytes, hash %08x", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), ok ? 1 : 0, pic.size(), GrimeFnv(pic));
    // Each peer's newest picture beside its log (`grime-<slot>.jpg`), to be compared by eye.
    if (ok) {
        char name[32];
        std::snprintf(name, sizeof(name), "grime-%u.jpg", static_cast<unsigned>(slot));
        if (FILE* f = std::fopen(name, "wb")) {
            std::fwrite(pic.data(), 1, pic.size(), f);
            std::fclose(f);
        }
    }
    if (host && t >= 140000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=rugs: rugs and stickers laid, and their custom pictures. HOST 40 s: a held custom rug
// (prop_rug_c_C), a laid custom rug (rug_c_C) and a sticker (sticker_C, the gamemode's first sticker) before it. CLIENT
// slot 1, 50 s: its own laid rug and sticker beside its body. Slot 1, 60 s and 65 s: picks the first custom rug picture
// on the held rug and the second on the laid rug nearest the host's body. HOST 80 s: picks the first on that rug. Every
// peer, each 5 s from 35 s (`[RUG]`): its laid rugs and stickers, their pictures, and the held rug's.
bool RugMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "rugs";
    return v;
}

// The gamemode's `names` list's `i`th entry, as a string.
std::wstring GmName(void* piece, const wchar_t* names, int i) {
    static R::InstanceOffset s_gm(L"gamemode");
    const int32_t go = piece ? s_gm.Of(piece) : -1;
    void* const gm = go >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(piece) + go) : nullptr;
    const int32_t no = gm ? R::FindPropertyOffset(R::ClassOf(gm), names) : -1;
    if (no < 0) return L"";
    struct Arr {
        R::FName* data;
        int32_t num, max;
    };
    const Arr* const a = reinterpret_cast<const Arr*>(static_cast<char*>(gm) + no);
    return i < a->num ? R::ToString(a->data[i]) : L"";
}

void* RugSpawn(const wchar_t* cls, const ue_wrap::FVector& at, const std::wstring& sticker) {
    const CR::Resolved rc = CR::ResolveClass(cls, CR::AnyFamily(), CR::Load::Immediate);
    void* const a = rc.cls ? E::BeginDeferredSpawn(rc.cls, at, {}) : nullptr;
    if (a && !sticker.empty()) {
        const int32_t off = R::FindPropertyOffset(R::ClassOf(a), L"name");
        if (off >= 0) ue_wrap::field_io::WriteFStringField(a, off, sticker);
    }
    return a && E::FinishDeferredSpawn(a, at, {}) ? a : nullptr;
}

std::wstring RugList(const wchar_t* cls, const wchar_t* field, bool fstr) {
    static void* found[64];
    const int n = ue_wrap::world_instances::Find(cls, found, 64);
    std::wstring out = std::to_wstring(n) + L"[";
    for (int i = 0; i < n; ++i) {
        const int32_t off = R::FindPropertyOffset(R::ClassOf(found[i]), field);
        std::wstring v = L"-";
        if (off >= 0) v = fstr ? ue_wrap::field_io::ReadFStringAt(found[i], off)
                               : R::ToString(*reinterpret_cast<R::FName*>(static_cast<char*>(found[i]) + off));
        out += (i ? L"," : L"") + v;
    }
    return out + L"]";
}

void RugStep(bool host, uint64_t t) {
    static int s_step = 0;
    void* const me = coop::players::Registry::Get().Local();
    coop::RemotePlayer* const hp = host ? nullptr : coop::players::Registry::Get().Puppet(0);
    void* const ref = host ? me : (hp ? hp->actor() : nullptr);
    void* const held = PicNearest(L"prop_rug_c_C", ref);
    void* const laid = PicNearest(L"rug_c_C", ref);
    const uint8_t slot = host ? 0 : static_cast<uint8_t>(coop::players::Registry::Get().LocalPeerId());
    auto before = [&](float fwd, float side) {
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        E::TryGetActorLocation(me, at);
        E::TryGetActorRotation(me, rot);
        const float yaw = rot.Yaw * 0.0174532925f;
        return ue_wrap::FVector{at.X + fwd * std::cos(yaw) - side * std::sin(yaw),
                                at.Y + fwd * std::sin(yaw) + side * std::cos(yaw), at.Z - 80.f};
    };
    if (host && s_step == 0 && t >= 40000u && me) {
        s_step = 1;
        void* const a = RugSpawn(L"prop_rug_c_C", before(200.f, -150.f), L"");
        void* const b = RugSpawn(L"rug_c_C", before(250.f, 0.f), L"");
        const std::wstring stk = GmName(b, L"filenames_stickers", 0);
        void* const c = RugSpawn(L"sticker_C", before(200.f, 150.f), stk);
        UE_LOGI("[RUG] host: held rug %d, laid rug %d, sticker '%ls' %d", a ? 1 : 0, b ? 1 : 0, stk.c_str(), c ? 1 : 0);
    }
    if (slot == 1 && s_step == 0 && t >= 50000u && me && laid) {
        s_step = 1;
        void* const b = RugSpawn(L"rug_c_C", before(150.f, -100.f), L"");
        const std::wstring stk = GmName(laid, L"filenames_stickers", 1);
        void* const c = RugSpawn(L"sticker_C", before(150.f, 100.f), stk);
        UE_LOGI("[RUG] client: its own laid rug %d, sticker '%ls' %d", b ? 1 : 0, stk.c_str(), c ? 1 : 0);
    }
    std::wstring picked;
    if (slot == 1 && s_step == 1 && t >= 60000u && held) {
        s_step = 2;
        const bool ran = PicPick(held, L"filenames_rugs", L"rugs", 0, picked);
        UE_LOGI("[RUG] client: picked '%ls' for the held rug (ran=%d)", picked.c_str(), ran ? 1 : 0);
    }
    if (slot == 1 && s_step == 2 && t >= 65000u && laid) {
        s_step = 3;
        const bool ran = PicPick(laid, L"filenames_rugs", L"rugs", 1, picked);
        UE_LOGI("[RUG] client: picked '%ls' for the laid rug (ran=%d)", picked.c_str(), ran ? 1 : 0);
    }
    if (host && s_step == 1 && t >= 80000u && laid) {
        s_step = 2;
        const bool ran = PicPick(laid, L"filenames_rugs", L"rugs", 0, picked);
        UE_LOGI("[RUG] host: picked '%ls' for the laid rug (ran=%d)", picked.c_str(), ran ? 1 : 0);
    }
    static uint64_t s_next = 0;
    if (t < 35000u || t > 150000u || t < s_next) return;
    s_next = t + 5000u;
    UE_LOGI("[RUG] %s t=%llu: laid rugs %ls; stickers %ls; held rug '%ls'", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), RugList(L"rug_c_C", L"filename", false).c_str(),
            RugList(L"sticker_C", L"name", true).c_str(), PicName(held).c_str());
    if (host && t >= 140000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=atvsound: HOST 40 s: an ATV spawned; each of its engine audio components' spatialization and
// attenuation (its own override, its attenuation asset, or its sound's), logged (`[ATVSND]`).
bool AtvSoundMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "atvsound";
    return v;
}

int SndFlag(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask) || off < 0) return -1;
    return (static_cast<uint8_t*>(obj)[off] & mask) ? 1 : 0;
}

void* SndObj(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    void* v = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(obj) + off) : nullptr;
    return v && R::IsLive(v) ? v : nullptr;
}

// An FSoundAttenuationSettings at `base`: whether it attenuates and spatializes, its falloff and its inner radius.
std::string SndSettings(void* st, const uint8_t* base) {
    if (!st || !base) return "-";
    auto boolAt = [&](const wchar_t* n) {
        int32_t off = -1;
        uint8_t mask = 0;
        return R::FindBoolProperty(st, n, off, mask) && off >= 0 ? ((base[off] & mask) ? 1 : 0) : -1;
    };
    auto floatAt = [&](const wchar_t* n) {
        const int32_t off = R::FindPropertyOffset(st, n);
        float v = -1.f;
        if (off >= 0) std::memcpy(&v, base + off, sizeof(v));
        return v;
    };
    const int32_t ext = R::FindPropertyOffset(st, L"AttenuationShapeExtents");
    float inner = -1.f;
    if (ext >= 0) std::memcpy(&inner, base + ext, sizeof(inner));
    char b[160];
    std::snprintf(b, sizeof(b), "attenuate=%d spatialize=%d falloff=%.0f cm inner=%.0f cm", boolAt(L"bAttenuate"),
                  boolAt(L"bSpatialize"), floatAt(L"FalloffDistance"), inner);
    return b;
}

void AtvSoundStep(bool host, uint64_t t) {
    static int s_step = 0;
    void* const me = coop::players::Registry::Get().Local();
    if (!host || !me) return;
    if (s_step == 0 && t >= 30000u) {
        s_step = 1;
        ue_wrap::FVector at{};
        E::TryGetActorLocation(me, at);
        at.X += 400.f;
        const CR::Resolved rc = CR::ResolveClass(L"ATV_C", CR::AnyFamily(), CR::Load::Immediate);
        UE_LOGI("[ATVSND] an ATV spawned=%d", rc.cls && E::SpawnActor(rc.cls, at) ? 1 : 0);
    }
    void* const atv = First(L"ATV_C");
    if (s_step != 1 || t < 35000u || !atv) return;
    s_step = 2;
    for (const wchar_t* n : {L"audio_car_start_cue", L"audio_car_drive_loop", L"audio_car_drive_start",
                             L"audio_car_drive_brake", L"audio_car_turbo", L"audio_car_stop"}) {
        void* const comp = SndObj(atv, n);
        void* const sound = SndObj(comp, L"Sound");
        void* const asset = SndObj(comp, L"AttenuationSettings");
        void* const soundAsset = SndObj(sound, L"AttenuationSettings");
        const int32_t ovOff = comp ? R::FindPropertyOffset(R::ClassOf(comp), L"AttenuationOverrides") : -1;
        void* const st = comp ? R::PropertyInnerStruct(R::ClassOf(comp), L"AttenuationOverrides") : nullptr;
        const std::string ov = ovOff >= 0 ? SndSettings(st, static_cast<uint8_t*>(comp) + ovOff) : "-";
        void* const use = asset ? asset : soundAsset;
        const int32_t aOff = use ? R::FindPropertyOffset(R::ClassOf(use), L"Attenuation") : -1;
        void* const ast = use ? R::PropertyInnerStruct(R::ClassOf(use), L"Attenuation") : nullptr;
        const std::string as = aOff >= 0 ? SndSettings(ast, static_cast<uint8_t*>(use) + aOff) : "-";
        UE_LOGI("[ATVSND] %ls: comp=%d sound='%ls' spatialization=%d override=%d (override: %s); attenuation asset "
                "'%ls' from %s (%s)", n, comp ? 1 : 0, sound ? R::ToString(R::NameOf(sound)).c_str() : L"-",
                SndFlag(comp, L"bAllowSpatialization"), SndFlag(comp, L"bOverrideAttenuation"), ov.c_str(),
                use ? R::ToString(R::NameOf(use)).c_str() : L"-", asset ? "the component" : (soundAsset ? "the sound" : "none"),
                as.c_str());
    }
    UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=fill: a client's bucket fill judged by the host's water. HOST 40 s: an empty bucket upright in the
// water volume nearest the host (its bottom 15 cm under the surface, its own tick off, so only an ask fills it) and an empty
// one 3 m before the host on dry land. CLIENT slot 1, 45 s: beside the wet bucket; 55 s: its copy of it to 30 cm (the
// game's own fill as the client sees it); 65 s: back beside the dry bucket; 75 s: its copy of that to 25 cm. Every peer,
// each 5 s (`[FILL]`): both buckets' water.
bool FillMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "fill";
    return v;
}

void FillStep(bool host, uint64_t t) {
    static int s_step = 0;
    static ue_wrap::FVector s_wetAt{}, s_dryAt{};
    static bool s_placed = false;
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    auto nearestBucket = [&](const ue_wrap::FVector& at) -> void* {
        static void* found[64];
        const int n = ue_wrap::world_instances::Find(L"prop_bucket_C", found, 64);
        void* best = nullptr;
        float bestD = 1e18f;
        for (int i = 0; i < n; ++i) {
            ue_wrap::FVector b{};
            if (!R::IsLive(found[i]) || !E::TryGetActorLocation(found[i], b)) continue;
            const float d = (b.X - at.X) * (b.X - at.X) + (b.Y - at.Y) * (b.Y - at.Y) + (b.Z - at.Z) * (b.Z - at.Z);
            if (d < bestD) {
                bestD = d;
                best = found[i];
            }
        }
        return bestD < 300.f * 300.f ? best : nullptr;
    };
    auto height = [](void* b) {
        ue_wrap::bucket::Plain p;
        return b && ue_wrap::bucket::ReadPlain(b, p) ? p.height : -1.f;
    };
    // Where the host put them: the host logs them; a client finds the water volume and the host's start the same way.
    if (!s_placed && t >= 38000u) {
        ue_wrap::FVector at{};
        E::TryGetActorLocation(host ? me : (coop::players::Registry::Get().Puppet(0)
                                                ? coop::players::Registry::Get().Puppet(0)->actor() : nullptr), at);
        static void* vols[64];
        const int n = ue_wrap::world_instances::Find(L"waterVolume_C", vols, 64);
        float bestD = 1e18f;
        for (int i = 0; i < n; ++i) {
            const int32_t bo = R::FindPropertyOffset(R::ClassOf(vols[i]), L"Box");
            const int32_t dw = R::FindPropertyOffset(R::ClassOf(vols[i]), L"depth_w");
            void* const box = bo >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(vols[i]) + bo) : nullptr;
            ue_wrap::FVector c{};
            if (!box || dw < 0 || !E::TryGetComponentLocation(box, c)) continue;
            float surface = 0.f;
            std::memcpy(&surface, static_cast<char*>(vols[i]) + dw, sizeof(surface));
            const float d = (c.X - at.X) * (c.X - at.X) + (c.Y - at.Y) * (c.Y - at.Y);
            if (d < bestD) {
                bestD = d;
                s_wetAt = ue_wrap::FVector{c.X, c.Y, surface - 15.f};
            }
        }
        ue_wrap::FRotator rot{};
        E::TryGetActorRotation(host ? me : nullptr, rot);
        s_dryAt = ue_wrap::FVector{at.X + 300.f, at.Y, at.Z - 60.f};
        s_placed = n > 0;
        UE_LOGI("[FILL] %s: %d water volumes; the nearest's water at (%.0f,%.0f,%.0f); dry spot (%.0f,%.0f,%.0f)",
                host ? "host" : "client", n, s_wetAt.X, s_wetAt.Y, s_wetAt.Z, s_dryAt.X, s_dryAt.Y, s_dryAt.Z);
    }
    if (!s_placed) return;
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        const CR::Resolved rc = CR::ResolveClass(L"prop_bucket_C", CR::AnyFamily(), CR::Load::Immediate);
        for (int i = 0; i < 2; ++i) {
            void* const b = rc.cls ? E::SpawnActor(rc.cls, i == 0 ? s_wetAt : s_dryAt) : nullptr;
            if (!b) continue;
            E::SetActorTickEnabled(b, false);
            const int32_t mo = R::FindPropertyOffset(R::ClassOf(b), L"StaticMesh");
            void* const mesh = mo >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(b) + mo) : nullptr;
            if (mesh) E::SetComponentSimulatePhysics(mesh, false);
            ue_wrap::bucket::WritePlain(b, 0.f, 0, 0.f, false, false);
            coop::bucket_sync::MarkDirty(b);
        }
        UE_LOGI("[FILL] host: a wet and a dry bucket spawned, empty, their own ticks off");
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    // The teleport refuses a place in water: the body is snapped there, again each tick through the wet phase so its
    // movement neither carries it off nor lets it fall through.
    if (slot1 && t >= 45000u && t < 64000u) {
        const ue_wrap::FVector to{s_wetAt.X + 150.f, s_wetAt.Y, s_wetAt.Z + 100.f};
        const bool ok = E::TeleportTo(me, to, {}) || E::SetActorLocation(me, to);
        if (s_step == 0) {
            UE_LOGI("[FILL] client: beside the wet bucket ok=%d", ok ? 1 : 0);
            s_step = 1;
        }
    }
    if (slot1 && s_step == 1 && t >= 55000u) {
        s_step = 2;
        void* const b = nearestBucket(s_wetAt);
        UE_LOGI("[FILL] client: its copy of the wet bucket to 30 (found=%d)",
                b && ue_wrap::bucket::WritePlain(b, 30.f, 0, 0.f, false, false) ? 1 : 0);
    }
    if (slot1 && s_step == 2 && t >= 65000u) {
        s_step = 3;
        const ue_wrap::FVector to{s_dryAt.X + 150.f, s_dryAt.Y, s_dryAt.Z + 100.f};
        UE_LOGI("[FILL] client: beside the dry bucket ok=%d", E::TeleportTo(me, to, {}) ? 1 : 0);
    }
    if (slot1 && s_step == 3 && t >= 75000u) {
        s_step = 4;
        void* const b = nearestBucket(s_dryAt);
        UE_LOGI("[FILL] client: its copy of the dry bucket to 25 (found=%d)",
                b && ue_wrap::bucket::WritePlain(b, 25.f, 0, 0.f, false, false) ? 1 : 0);
    }
    // HOST: each frame, a drop of the wet bucket's water names what its Sphere overlaps (the game's splash takes 1-2 cm off
    // a bucket over 10 cm when anything enters the sphere).
    static float s_lastWet = -1.f;
    if (host && s_step >= 1) {
        void* const wet = nearestBucket(s_wetAt);
        const float h = height(wet);
        if (wet && s_lastWet >= 0.f && std::fabs(h - s_lastWet) > 0.01f) {
            const int32_t so = R::FindPropertyOffset(R::ClassOf(wet), L"Sphere");
            void* const sphere = so >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(wet) + so) : nullptr;
            std::wstring names;
            ue_wrap::ParamFrame f(sphere ? R::FindDispatchFunctionCached(R::ClassOf(sphere), L"GetOverlappingActors")
                                         : nullptr);
            if (f.valid() && ue_wrap::Call(sphere, f)) {
                struct Arr {
                    void** data;
                    int32_t num, max;
                };
                const Arr a = f.Get<Arr>(L"OverlappingActors");
                ue_wrap::FVector wa{};
                E::TryGetActorLocation(wet, wa);
                for (int32_t i = 0; i < a.num && a.data; ++i) {
                    if (!R::IsLive(a.data[i])) continue;
                    ue_wrap::FVector oa{};
                    E::TryGetActorLocation(a.data[i], oa);
                    const float d = std::sqrt((oa.X - wa.X) * (oa.X - wa.X) + (oa.Y - wa.Y) * (oa.Y - wa.Y) +
                                              (oa.Z - wa.Z) * (oa.Z - wa.Z));
                    names += (names.empty() ? L"" : L", ") + R::ToString(R::NameOf(R::ClassOf(a.data[i]))) +
                             (a.data[i] == wet ? L" (itself)" : L" (another, " + std::to_wstring(static_cast<int>(d)) + L" cm)");
                }
                if (a.data) R::EngineFree(a.data);
            }
            UE_LOGI("[FILL] host: the wet bucket's water went %.1f -> %.1f; its sphere overlaps [%ls]", s_lastWet, h,
                    names.c_str());
        }
        s_lastWet = h;
    }
    static uint64_t s_next = 0;
    if (t < 42000u || t > 120000u || t < s_next) return;
    s_next = t + 5000u;
    UE_LOGI("[FILL] %s t=%llu: wet bucket %.1f, dry bucket %.1f", host ? "host" : "client",
            static_cast<unsigned long long>(t / 1000u), height(nearestBucket(s_wetAt)), height(nearestBucket(s_dryAt)));
    if (host && t >= 110000u) UE_LOGI("[ENTITY] done");
}

// VOTVCOOP_ENTITY_PROBE=mopdip: whether a client's mop gets wet from a bucket each press. HOST 40 s: a full bucket 1.5 m
// before it (its own tick on). CLIENT slot 1, 48 s: a mop in its hand; from 52 s it looks at the bucket every frame, and
// each 3 s from 55 s (ten presses) its mop is made dry (`power` 0) and pressed on the bucket through the mop's own
// playerHandUse_LMB. 2.5 s after each press (`[MOP]`): the mop's power and both copies' water.
bool MopMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "mopdip";
    return v;
}

void MopStep(bool host, uint64_t t) {
    static int s_step = 0;
    static int s_press = 0;
    static float s_before = 0.f;
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    void* const bucket = First(L"prop_bucket_C");
    auto water = [](void* b) {
        ue_wrap::bucket::Plain p;
        return b && ue_wrap::bucket::ReadPlain(b, p) ? p.height : -1.f;
    };
    static R::InstanceOffset s_holding(L"holding_actor");
    static R::InstanceOffset s_power(L"power");
    auto held = [&]() -> void* {
        const int32_t off = s_holding.Of(me);
        void* const h = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(me) + off) : nullptr;
        return h && R::IsLive(h) ? h : nullptr;
    };
    auto power = [&](void* mop) -> float* {
        const int32_t off = mop ? s_power.Of(mop) : -1;
        return off >= 0 ? reinterpret_cast<float*>(static_cast<char*>(mop) + off) : nullptr;
    };
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        E::TryGetActorLocation(me, at);
        E::TryGetActorRotation(me, rot);
        const float yaw = rot.Yaw * 0.0174532925f;
        const ue_wrap::FVector to{at.X + 150.f * std::cos(yaw), at.Y + 150.f * std::sin(yaw), at.Z - 60.f};
        const CR::Resolved rc = CR::ResolveClass(L"prop_bucket_C", CR::AnyFamily(), CR::Load::Immediate);
        void* const b = rc.cls ? E::SpawnActor(rc.cls, to) : nullptr;
        if (b) {
            ue_wrap::bucket::WritePlain(b, 38.f, 0, 0.f, false, false);
            ue_wrap::bucket::CallUpd(b);
            coop::bucket_sync::MarkDirty(b);
        }
        UE_LOGI("[MOP] host: a full bucket spawned=%d", b ? 1 : 0);
    }
    const bool slot1 = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if (!slot1) {
        static uint64_t s_next = 0;
        if (host && t >= 45000u && t >= s_next && t <= 95000u) {
            s_next = t + 5000u;
            UE_LOGI("[MOP] host t=%llu: its bucket %.1f", static_cast<unsigned long long>(t / 1000u), water(bucket));
        }
        if (host && t >= 96000u && s_step == 1) {
            s_step = 2;
            UE_LOGI("[ENTITY] done");
        }
        return;
    }
    if (s_step == 0 && t >= 48000u) {
        s_step = 1;
        UE_LOGI("[MOP] client: a mop in hand ok=%d",
                coop::dev::drill_hand::TakeIntoHandNow(L"mop", L"prop_mop_C") ? 1 : 0);
    }
    // VOTVCOOP_MOPDIP_AWAY=1: the view turns from the bucket in the frame after each press and back 1.5 s later, so the
    // grant's replay of the click traces past it.
    static const bool s_away = coop::config::ReadEnv("VOTVCOOP_MOPDIP_AWAY") == "1";
    static uint64_t s_awayUntil = 0;
    ue_wrap::FVector eye{}, b{};
    if (t >= 52000u && bucket && E::TryGetActorLocation(bucket, b) && E::ReadMainPlayerCameraLocation(me, eye)) {
        ue_wrap::FRotator look = coop::director::LookAt(eye, ue_wrap::FVector{b.X, b.Y, b.Z + 20.f});
        if (t < s_awayUntil) look.Yaw += 180.f;
        E::SetControlRotation(E::GetController(me), look);
    }
    const uint64_t pressAt = 55000u + 3000u * static_cast<uint64_t>(s_press);
    if (s_press < 10 && s_step == 1 && t >= pressAt) {
        void* const mop = held();
        if (float* const pw = power(mop)) *pw = 0.f;
        s_before = water(bucket);
        const bool ran = mop && ue_wrap::bucket::CallHandUseLmb(mop, me);
        if (s_away) s_awayUntil = t + 1500u;
        UE_LOGI("[MOP] client: press %d (held %ls, ran=%d), its bucket %.1f", s_press + 1,
                mop ? R::ToString(R::NameOf(R::ClassOf(mop))).c_str() : L"nothing", ran ? 1 : 0, s_before);
        s_step = 2;
    }
    if (s_step == 2 && t >= pressAt + 2500u) {
        void* const mop = held();
        const float* const pw = power(mop);
        const float now = water(bucket);
        UE_LOGI("[MOP] client: press %d -> mop power %.2f (%s), its bucket %.1f -> %.1f (%.1f taken)", s_press + 1,
                pw ? *pw : -1.f, pw && *pw > 0.f ? "wet" : "DRY", s_before, now, s_before - now);
        ++s_press;
        s_step = 1;
    }
}

// VOTVCOOP_ENTITY_PROBE=bombbox: a client opens the Arirals' bomb box. HOST 40 s: a bomb box 2.5 m before it. CLIENT
// slot 1, 55 s: its copy opened with action 4 (the box's own actionOptionIndex, as the player's press calls it). Every
// peer, each 5 s from 42 s (`[BOMB]`): how many bomb boxes stand in its world.
bool BombMode() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "bombbox";
    return v;
}

void BombStep(bool host, uint64_t t) {
    static int s_step = 0;
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    if (host && s_step == 0 && t >= 40000u) {
        s_step = 1;
        ue_wrap::FVector at{};
        ue_wrap::FRotator rot{};
        E::TryGetActorLocation(me, at);
        E::TryGetActorRotation(me, rot);
        const float yaw = rot.Yaw * 0.0174532925f;
        const ue_wrap::FVector to{at.X + 250.f * std::cos(yaw), at.Y + 250.f * std::sin(yaw), at.Z - 40.f};
        const CR::Resolved rc = CR::ResolveClass(L"prop_bombbox_C", CR::AnyFamily(), CR::Load::Immediate);
        UE_LOGI("[BOMB] host: a bomb box spawned=%d", rc.cls && E::SpawnActor(rc.cls, to) ? 1 : 0);
    }
    void* const box = First(L"prop_bombbox_C");
    if (!host && coop::players::Registry::Get().LocalPeerId() == 1 && s_step == 0 && t >= 55000u && box) {
        s_step = 1;
        ue_wrap::FVector at{};
        E::TryGetActorLocation(box, at);
        UE_LOGI("[BOMB] client: opened its bomb box copy (ran=%d)",
                ue_wrap::prop_use::CallActionOptionIndex(box, me, nullptr, at, 4, nullptr) ? 1 : 0);
    }
    static uint64_t s_next = 0;
    if (t < 42000u || t > 100000u || t < s_next) return;
    s_next = t + 5000u;
    static void* found[16];
    UE_LOGI("[BOMB] %s t=%llu: %d bomb boxes", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u),
            ue_wrap::world_instances::Find(L"prop_bombbox_C", found, 16));
    if (host && t >= 95000u) UE_LOGI("[ENTITY] done");
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
    if (PhotoMode()) {
        PhotoStep(host, t);
        return;
    }
    if (PourMode()) {
        PourStep(host, t);
        return;
    }
    if (AtvMode()) {
        AtvStep(host, t);
        return;
    }
    if (GasMode()) {
        GasStep(host, t);
        return;
    }
    if (PicMode()) {
        PicStep(host, t);
        return;
    }
    if (ZipMode()) {
        ZipStep(host, t);
        return;
    }
    if (GrimeMode()) {
        GrimeStep(host, t);
        return;
    }
    if (RugMode()) {
        RugStep(host, t);
        return;
    }
    if (AtvSoundMode()) {
        AtvSoundStep(host, t);
        return;
    }
    if (FillMode()) {
        FillStep(host, t);
        return;
    }
    if (MopMode()) {
        MopStep(host, t);
        return;
    }
    if (BombMode()) {
        BombStep(host, t);
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
