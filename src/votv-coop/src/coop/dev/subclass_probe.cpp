// coop/dev/subclass_probe.cpp -- [dev] the exact-class sweep's fixes on the rig (VOTVCOOP_SUBCLASS_PROBE=<mode> on every
// peer). Times run from the probe's arm; client means slot 1; the host ends a mode with `[SUBCLASS] DONE`.
//   1        60 s the host spawns a raw patty and a blue brain cheese; 80 s the client presses the patty's action 4,
//            then the rpg mode's shot at 95 s.
//   rpg      both set the gamemode's unlockFungunsInStory; 70 s the client takes the rapid RPG and fires it.
//   atvtape  60 s the host takes the first ATV to 40 health; the client stands by it with duct tape, runs toolboxFix.
//   lamb     60 s the host spawns a killer lamb, no wisp in the world. walker: a prop_wAlien_C.
//   trigger  60 s the host gives the world's counting box nearest the client a trigger_openLevel_C target; 64 s the
//            client steps in. triggerhost: the host's own player into that box, its targets emptied.
//   drive    (VOTVCOOP_RACK_DENY_TAKES=1 on the host) 60 s the host spawns a drive box holding a row; 80 s the client
//            takes it out as the box's take does, into an explosive drive (VOTVCOOP_SUBCLASS_DRIVE for another class).

#include "coop/dev/subclass_probe.h"

#include "coop/config/config.h"
#include "coop/dev/drill_hand.h"
#include "coop/dev/spawn_npc.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/hand_item.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/prop_use.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/desk/drive_chain.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace coop::dev::subclass_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
namespace CR = ue_wrap::class_resolve;
namespace A  = ue_wrap::atv;
namespace DC = ue_wrap::drive_chain;
using ue_wrap::FVector;

const std::string& Mode() {
    static const std::string m = coop::config::ReadEnv("VOTVCOOP_SUBCLASS_PROBE");
    return m;
}

uint64_t Now() { return ::GetTickCount64(); }

void* Load(const wchar_t* cls) { return CR::ResolveClass(cls, CR::AnyFamily(), CR::Load::Immediate).cls; }

std::vector<void*> All(const wchar_t* cls) {
    std::vector<void*> out;
    if (void* const c = OI::ClassByName(cls))
        OI::ForEachInstance(c, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

unsigned Eid(void* a) { return static_cast<unsigned>(coop::element::Registry::Get().EidForActor(a)); }

void Report(const char* role, std::initializer_list<const wchar_t*> classes) {
    for (const wchar_t* cls : classes) {
        std::string eids;
        const std::vector<void*> all = All(cls);
        for (void* a : all) eids += " " + std::to_string(Eid(a));
        UE_LOGI("[SUBCLASS] %s %ls x%zu eids:%s", role, cls, all.size(), eids.c_str());
    }
}

bool CallWithPlayer(void* obj, void* player, const wchar_t* verb) {
    void* fn = obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), verb) : nullptr;
    ue_wrap::ParamFrame f(fn);
    return fn && f.valid() && f.Set<void*>(L"player", player) && ue_wrap::Call(obj, f);
}

int32_t* IntField(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? reinterpret_cast<int32_t*>(static_cast<uint8_t*>(obj) + off) : nullptr;
}

bool WriteBool(void* obj, const wchar_t* name, bool v) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return false;
    uint8_t* b = static_cast<uint8_t*>(obj) + off;
    *b = v ? static_cast<uint8_t>(*b | mask) : static_cast<uint8_t>(*b & ~mask);
    return true;
}

void* Spawn(const wchar_t* cls, const FVector& at) {
    void* const c = Load(cls);
    void* const a = c ? E::BeginDeferredSpawn(c, at, {}) : nullptr;
    return a && E::FinishDeferredSpawn(a, at, {}) ? a : nullptr;
}

FVector Ahead(void* me, float cm) {
    FVector at{};
    E::TryGetActorLocation(me, at);
    const FVector fwd = E::GetActorForwardVector(me);
    return {at.X + fwd.X * cm, at.Y + fwd.Y * cm, at.Z};
}

void* HandActor(void* me) {
    E::MainPlayerGrabState gs{};
    return me && E::ReadMainPlayerGrabState(me, gs) ? gs.holdingActor : nullptr;
}

// A class's list_props row: its class default's name.
std::wstring RowOf(const wchar_t* cls) {
    Load(cls);
    void* const cdo = R::FindClassDefaultObject(cls);
    return cdo ? R::ToString(ue_wrap::prop::GetPropName(cdo)) : std::wstring();
}

uint64_t g_armedAt = 0, g_reportAt = 0;
int g_step = 0;
bool g_done = false;

// ---- 1 and rpg ----
void TickRpg(uint64_t t, bool host, void* me, uint64_t from) {
    static int s = 0;
    if (t + 10000 >= from && s == 0) {
        s = 1;
        UE_LOGI("[SUBCLASS] %s: the gamemode's unlockFungunsInStory set (%d)", host ? "host" : "client",
                WriteBool(ue_wrap::world_singleton::Gamemode(), L"unlockFungunsInStory", true) ? 1 : 0);
    }
    if (host) return;
    if (s == 1 && t >= from) {
        s = 2;
        const std::wstring row = RowOf(L"prop_funGun_rpg_rapid_C");
        UE_LOGI("[SUBCLASS] client: the rapid RPG (row '%ls') into the hand (taken %d)", row.c_str(),
                coop::dev::drill_hand::TakeIntoHandNow(row.c_str(), L"prop_funGun_rpg_rapid_C") ? 1 : 0);
    }
    if (s == 2 && t >= from + 2000) {
        void* const held = HandActor(me);
        if (!coop::hand_item::OwnAnnouncedA(L"prop_funGun_rpg_C")) {
            if (t >= from + 15000) {
                s = 3;
                UE_LOGW("[SUBCLASS] client: the rapid RPG was never announced (holding %ls)",
                        held ? R::ClassNameOf(held).c_str() : L"none");
            }
            return;
        }
        s = 3;
        int32_t* const ammo = IntField(held, L"ammo_primary");
        int32_t* const mag = IntField(held, L"maxMagazine_primary");
        if (ammo && mag) *ammo = *mag;
        UE_LOGI("[SUBCLASS] client: holding %ls, ammo %d, fired (LMB ran=%d)", held ? R::ClassNameOf(held).c_str() : L"none",
                ammo ? *ammo : -1, CallWithPlayer(held, me, L"playerHandUse_LMB") ? 1 : 0);
    }
}

void TickConvert(uint64_t t, bool host, void* me) {
    static int s = 0;
    if (host && s == 0 && t >= 60000) {
        s = 1;
        const FVector p = Ahead(me, 150.f);
        void* const patty = Spawn(L"prop_cookingFood_rawPatty_C", p);
        void* const cheese = Spawn(L"prop_food_blueBrainCheese_C", FVector{p.X, p.Y + 60.f, p.Z});
        UE_LOGI("[SUBCLASS] host: spawned a raw patty (%d) and a blue brain cheese (%d)", patty ? 1 : 0, cheese ? 1 : 0);
    }
    if (!host && s == 0 && t >= 80000) {
        s = 1;
        void* patty = nullptr;
        for (void* p : All(L"prop_cookingFood_rawPatty_C"))
            if (Eid(p)) patty = p;
        FVector at{};
        if (patty) E::TryGetActorLocation(patty, at);
        const bool ran = patty && ue_wrap::prop_use::CallActionOptionIndex(patty, me, nullptr, at, 4, nullptr);
        UE_LOGI("[SUBCLASS] client: pressed action 4 on raw patty eid=%u (ran=%d)", patty ? Eid(patty) : 0, ran ? 1 : 0);
    }
}

// ---- atvtape ----
void* FirstAtv() {
    void* atv = nullptr;
    if (A::EnsureResolved())
        A::ForEachAtv([](void* ctx, void* a, int32_t) {
            if (!*static_cast<void**>(ctx)) *static_cast<void**>(ctx) = a;
        }, &atv);
    return atv;
}

void TickAtvTape(uint64_t t, bool host, void* me) {
    void* const atv = FirstAtv();
    if (host && g_step == 0 && t >= 60000) {
        g_step = 1;
        const bool set = atv && A::SetHealth(atv, 40.f) && A::CallUpdHealth(atv);
        UE_LOGI("[SUBCLASS] host: ATV '%ls' to 40 health (%d)", atv ? A::GetKeyString(atv).c_str() : L"none", set ? 1 : 0);
    }
    if (host || !atv || !me) return;
    if (g_step == 0 && t >= 65000) {
        g_step = 1;
        FVector at{};
        E::TryGetActorLocation(atv, at);
        const bool moved = E::TeleportTo(me, {at.X + 220.f, at.Y, at.Z + 60.f}, {});
        A::SetHealth(atv, 40.f);
        A::CallUpdHealth(atv);
        const std::wstring row = RowOf(L"prop_ducttape_C");
        const bool taken = coop::dev::drill_hand::TakeIntoHandNow(row.c_str(), L"prop_ducttape_C");
        UE_LOGI("[SUBCLASS] client: beside ATV '%ls' (moved %d), its copy at %.0f health, duct tape (row '%ls') in the "
                "hand (%d)", A::GetKeyString(atv).c_str(), moved ? 1 : 0, A::GetHealth(atv), row.c_str(), taken ? 1 : 0);
    }
    if (g_step == 1 && t >= 75000) {
        g_step = 2;
        void* const held = HandActor(me);
        void* fn = R::FindDispatchFunctionCached(R::ClassOf(atv), L"toolboxFix");
        ue_wrap::ParamFrame f(fn);
        if (f.valid()) {
            f.Set<void*>(L"toolbox", held);
            f.Set<void*>(L"player", me);
        }
        const bool ran = fn && f.valid() && ue_wrap::Call(atv, f);
        UE_LOGI("[SUBCLASS] client: holding %ls, toolboxFix on the ATV (ran=%d), its copy now %.0f health",
                held ? R::ClassNameOf(held).c_str() : L"none", ran ? 1 : 0, A::GetHealth(atv));
    }
}

// ---- trigger ----
void* PuppetOf(uint8_t slot) {
    void* bodies[16] = {};
    const int n = coop::players::Registry::Get().LiveBodies(bodies, 16);
    for (int i = 0; i < n; ++i)
        if (coop::players::Registry::Get().IsPuppetBody(bodies[i]) &&
            coop::players::Registry::Get().PeerIdOfBody(bodies[i]) == slot)
            return bodies[i];
    return nullptr;
}

// The box's TArray<AActor*> `objects`, made one element: written as two int32 and its count set to 1.
bool SetOneTarget(void* box, void* target) {
    const int32_t off = box ? R::FindPropertyOffset(R::ClassOf(box), L"objects") : -1;
    if (off < 0 || !target) return false;
    const uint64_t p = reinterpret_cast<uint64_t>(target);
    const std::vector<int32_t> two{static_cast<int32_t>(p & 0xffffffffu), static_cast<int32_t>(p >> 32)};
    if (!ue_wrap::field_io::WriteInt32ArrayField(box, off, two)) return false;
    *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(box) + off + 8) = 1;
    return true;
}

FVector g_spot{};
// The world's trigger_box_N_C nearest `from` that still counts (n above 0).
void* NearestBox(const FVector& from) {
    void* best = nullptr;
    float bestD = 1e18f;
    for (void* box : All(L"trigger_box_N_C")) {
        int32_t* const n = IntField(box, L"n");
        FVector at{};
        if (!n || *n <= 0 || !E::TryGetActorLocation(box, at)) continue;
        const float d = (at.X - from.X) * (at.X - from.X) + (at.Y - from.Y) * (at.Y - from.Y) + (at.Z - from.Z) * (at.Z - from.Z);
        if (d < bestD) { bestD = d; best = box; }
    }
    return best;
}

// A box spawned at run time never counts a touch (its collision is set where the level places it), so the drill uses
// the world's own box nearest the client, and gives it one target, a trigger_openLevel_C.
void TickTrigger(uint64_t t, bool host, void* me) {
    if (host && g_step == 0 && t >= 60000) {
        g_step = 1;
        void* const body = PuppetOf(1);
        FVector at{}, boxAt{};
        if (!body || !E::TryGetActorLocation(body, at)) {
            UE_LOGW("[SUBCLASS] host: no body of slot 1 here");
            return;
        }
        void* const box = NearestBox(at);
        if (box) E::TryGetActorLocation(box, boxAt);
        void* const target = Spawn(L"trigger_openLevel_C", {at.X, at.Y, at.Z - 2000.f});
        int32_t* const n = IntField(box, L"n");
        const bool set = SetOneTarget(box, target);
        UE_LOGI("[SUBCLASS] host: the world's trigger_box_N_C nearest slot 1 (%d, n %d) at (%.0f,%.0f,%.0f), its one target "
                "now a trigger_openLevel_C (%d, set %d)", box ? 1 : 0, n ? *n : -1, boxAt.X, boxAt.Y, boxAt.Z,
                target ? 1 : 0, set ? 1 : 0);
    }
    if (host || !me) return;
    if (g_step == 0 && t >= 64000) {
        g_step = 1;
        E::TryGetActorLocation(me, g_spot);
        void* const box = NearestBox(g_spot);
        FVector at{};
        if (box) E::TryGetActorLocation(box, at);
        UE_LOGI("[SUBCLASS] client: into the box at (%.0f,%.0f,%.0f) (%d)", at.X, at.Y, at.Z,
                box && E::TeleportTo(me, at, {}) ? 1 : 0);
    }
    if (g_step == 1 && t >= 70000) {
        g_step = 2;
        UE_LOGI("[SUBCLASS] client: back out (%d)", E::TeleportTo(me, g_spot, {}) ? 1 : 0);
    }
}

// ---- drive ----
void* FirstRack() {
    for (void* r : All(L"prop_driveRack_C"))
        if (Eid(r)) return r;
    return nullptr;
}

void TickDrive(uint64_t t, bool host, void* me) {
    if (host && g_step == 0 && t >= 60000) {
        g_step = 1;
        void* const rack = Spawn(L"prop_driveRack_C", Ahead(me, 150.f));
        DC::RackRow r;
        r.has = true;
        r.row.name = L"mvsub_0";
        r.row.id = r.row.name;
        r.row.object = L"mvsub_object";
        r.row.signal = L"mvsub_signal";
        r.row.size = 10.f;
        r.row.decoded = 10.f;
        r.row.hasData = true;
        const bool ok = rack && DC::WriteRackRow(rack, 0, r) && DC::CallRackGen(rack);
        UE_LOGI("[SUBCLASS] host: a drive box (%d) holding one row (%d)", rack ? 1 : 0, ok ? 1 : 0);
    }
    if (host || !me) return;
    if (g_step == 0 && t >= 80000) {
        g_step = 1;
        void* const rack = FirstRack();
        DC::RackRow rows[DC::kRackSlots];
        int n = 0;
        if (!rack || !DC::ReadRack(rack, rows, n) || !rows[0].has) {
            UE_LOGW("[SUBCLASS] client: no drive box holding a row 0 here (%d)", rack ? 1 : 0);
            return;
        }
        FVector at{};
        E::TryGetActorLocation(rack, at);
        // VOTVCOOP_SUBCLASS_DRIVE: the drive's class (prop_drive_C for the control).
        static const std::wstring cls = [] {
            const std::string v = coop::config::ReadEnv("VOTVCOOP_SUBCLASS_DRIVE");
            return v.empty() ? std::wstring(L"prop_drive_explosive_C") : std::wstring(v.begin(), v.end());
        }();
        void* const drive = Spawn(cls.c_str(), {at.X, at.Y, at.Z + 80.f});
        const bool wrote = drive && DC::WriteDriveRow(drive, rows[0].row);
        DC::RackRow empty;
        const bool cleared = DC::WriteRackRow(rack, 0, empty) && DC::CallRackGen(rack);
        UE_LOGI("[SUBCLASS] client: took row 0 of box eid=%u: a drive %p (row written %d, its eid %u), the row "
                "cleared (%d)", Eid(rack), drive, wrote ? 1 : 0, drive ? Eid(drive) : 0, cleared ? 1 : 0);
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Mode().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!host && coop::players::Registry::Get().LocalPeerId() != 1) return;
    if (!g_armedAt) {
        g_armedAt = Now();
        UE_LOGI("[SUBCLASS] probe armed role=%s mode=%s", host ? "host" : "client", Mode().c_str());
    }
    const uint64_t t = Now() - g_armedAt;
    void* const me = coop::players::Registry::Get().Local();
    const char* const role = host ? "host" : "client";
    const std::string& m = Mode();
    if (m == "1") {
        TickConvert(t, host, me);
        TickRpg(t, host, me, 95000);
    } else if (m == "rpg") {
        TickRpg(t, host, me, 70000);
    } else if (m == "atvtape") {
        TickAtvTape(t, host, me);
    } else if (m == "lamb" && host && g_step == 0 && t >= 60000) {
        g_step = 1;
        const FVector p = Ahead(me, 400.f);
        Load(L"killerlamb_C");
        void* const lamb = coop::dev::spawn_npc::SpawnAt(L"killerlamb_C", {p.X, p.Y, p.Z + 50.f});
        UE_LOGI("[SUBCLASS] host: spawned a killer lamb %p (wisps in the world: %zu)", lamb, All(L"killerwisp_C").size());
    } else if (m == "walker" && host && g_step == 0 && t >= 60000) {
        g_step = 1;
        UE_LOGI("[SUBCLASS] host: spawned a prop_wAlien_C %p", Spawn(L"prop_wAlien_C", Ahead(me, 300.f)));
    } else if (m == "trigger") {
        TickTrigger(t, host, me);
    } else if (m == "triggerhost" && host && me) {
        // The control: the host's own player into a target-less box, 3 m ahead and back.
        static void* s_box = nullptr;
        static FVector s_home{};
        if (g_step == 0 && t >= 60000) {
            g_step = 1;
            E::TryGetActorLocation(me, s_home);
            // The world's own box nearest this player, its targets emptied, so the touch runs nothing of the world's.
            s_box = NearestBox(s_home);
            const int32_t off = s_box ? R::FindPropertyOffset(R::ClassOf(s_box), L"objects") : -1;
            if (off >= 0) *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(s_box) + off + 8) = 0;
            int32_t* const n = IntField(s_box, L"n");
            UE_LOGI("[SUBCLASS] host: the world's nearest trigger_box_N_C (%d, n %d), its targets emptied (%d)",
                    s_box ? 1 : 0, n ? *n : -1, off >= 0 ? 1 : 0);
        }
        if (g_step == 1 && t >= 64000) {
            g_step = 2;
            FVector at{};
            E::TryGetActorLocation(s_box, at);
            UE_LOGI("[SUBCLASS] host: into the box (%d)", E::TeleportTo(me, at, {}) ? 1 : 0);
        }
        if (g_step == 2 && t >= 68000) {
            g_step = 3;
            int32_t* const n = IntField(s_box, L"n");
            UE_LOGI("[SUBCLASS] host: back out (%d), the box's n=%d", E::TeleportTo(me, s_home, {}) ? 1 : 0, n ? *n : -1);
        }
    } else if (m == "drive") {
        TickDrive(t, host, me);
    }
    if (host && !g_done && t >= 150000) {
        g_done = true;
        UE_LOGI("[SUBCLASS] DONE");
    }
    if (t < 55000 || t > 160000 || Now() < g_reportAt) return;
    g_reportAt = Now() + 5000;
    if (m == "1") Report(role, {L"prop_cookingFood_rawPatty_C", L"prop_cookingFood_rawSausage_C", L"prop_food_blueBrainCheese_C"});
    if (m == "1" || m == "rpg") Report(role, {L"prop_rocket_C"});
    if (m == "lamb") Report(role, {L"killerlamb_C", L"killerwisp_C"});
    if (m == "walker") Report(role, {L"prop_wAlien_C", L"prop_wMannequin_C"});
    if (m == "drive") Report(role, {L"prop_drive_explosive_C", L"prop_drive_C", L"prop_driveRack_C"});
    if (m == "trigger" && host)
        for (void* box : All(L"trigger_box_N_C")) {
            int32_t* const n = IntField(box, L"n");
            FVector at{}, body{};
            E::TryGetActorLocation(box, at);
            if (void* const b = PuppetOf(1)) E::TryGetActorLocation(b, body);
            if ((at.X - body.X) * (at.X - body.X) + (at.Y - body.Y) * (at.Y - body.Y) > 1000.f * 1000.f) continue;
            // Its filter (TArray<UClass*>): the classes whose touch it counts, none meaning any.
            std::wstring filter;
            const int32_t fo = R::FindPropertyOffset(R::ClassOf(box), L"filter");
            if (fo >= 0) {
                void* const* data = *reinterpret_cast<void* const* const*>(static_cast<uint8_t*>(box) + fo);
                const int32_t num = *reinterpret_cast<const int32_t*>(static_cast<uint8_t*>(box) + fo + 8);
                for (int32_t i = 0; data && i < num && i < 8; ++i) filter += L" " + R::ToString(R::NameOf(data[i]));
            }
            UE_LOGI("[SUBCLASS] host box n=%d at (%.0f,%.0f,%.0f), filter:%ls, slot 1's body at (%.0f,%.0f,%.0f)",
                    n ? *n : -1, at.X, at.Y, at.Z, filter.c_str(), body.X, body.Y, body.Z);
        }
    if (m == "atvtape")
        if (void* const atv = FirstAtv()) UE_LOGI("[SUBCLASS] %s ATV health %.0f", role, A::GetHealth(atv));
}

}  // namespace coop::dev::subclass_probe
