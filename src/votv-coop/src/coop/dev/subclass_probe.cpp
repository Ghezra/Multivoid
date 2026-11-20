// coop/dev/subclass_probe.cpp -- [dev] the exact-class sweep's fixes on the rig (VOTVCOOP_SUBCLASS_PROBE=1 on every
// peer). Times run from the probe's arm. THE HOST at 60 s spawns a raw patty and a blue brain cheese 1.5 m before its
// player. CLIENT SLOT 1 at 80 s presses the patty's action 4 (its own actionOptionIndex: a raw sausage made, the patty
// destroyed), at 95 s takes the rapid RPG (prop_funGun_rpg_rapid_C, a prop_funGun_rpg_C subclass) into its hand, and
// once its own announce of it is queued, loads it and fires it as the gun's LMB does. EVERY PEER, every 5 s from 55 s:
// the patties, sausages, cheeses and rockets it holds, with their element ids (`[SUBCLASS]`). The host ends at 150 s.

#include "coop/dev/subclass_probe.h"

#include "coop/config/config.h"
#include "coop/dev/drill_hand.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/hand_item.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/prop_use.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace coop::dev::subclass_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
using ue_wrap::FVector;

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_SUBCLASS_PROBE") == "1";
    return on;
}

uint64_t Now() { return ::GetTickCount64(); }

// The class, loaded by its asset path when nothing has loaded it yet.
void* Load(const wchar_t* leaf) {
    if (void* c = OI::ClassByName((std::wstring(leaf) + L"_C").c_str())) return c;
    const std::wstring path = L"/Game/objects/" + std::wstring(leaf) + L"." + leaf + L"_C";
    return ue_wrap::asset_load::LoadObjectByPath(path.c_str());
}

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

void Report(const char* role) {
    for (const wchar_t* cls : {L"prop_cookingFood_rawPatty_C", L"prop_cookingFood_rawSausage_C",
                               L"prop_food_blueBrainCheese_C", L"prop_rocket_C"}) {
        std::string eids;
        const std::vector<void*> all = All(cls);
        for (void* a : all) eids += " " + std::to_string(Eid(a));
        UE_LOGI("[SUBCLASS] %s %ls x%zu eids:%s", role, cls, all.size(), eids.c_str());
    }
}

bool CallHandVerb(void* held, void* player, const wchar_t* verb) {
    void* fn = held ? R::FindDispatchFunctionCached(R::ClassOf(held), verb) : nullptr;
    ue_wrap::ParamFrame f(fn);
    return fn && f.valid() && f.Set<void*>(L"player", player) && ue_wrap::Call(held, f);
}

int32_t* IntField(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? reinterpret_cast<int32_t*>(static_cast<uint8_t*>(obj) + off) : nullptr;
}

void* Spawn(void* cls, const FVector& at) {
    void* const a = cls ? E::BeginDeferredSpawn(cls, at, {}) : nullptr;
    return a && E::FinishDeferredSpawn(a, at, {}) ? a : nullptr;
}

uint64_t g_armedAt = 0, g_reportAt = 0;
int g_step = 0;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const bool actor = !host && coop::players::Registry::Get().LocalPeerId() == 1;
    if (!host && !actor) return;
    if (!g_armedAt) {
        g_armedAt = Now();
        UE_LOGI("[SUBCLASS] probe armed role=%s", host ? "host" : "client");
    }
    const uint64_t t = Now() - g_armedAt;
    void* const me = coop::players::Registry::Get().Local();
    if (host && g_step == 0 && t >= 60000) {
        g_step = 1;
        FVector at{};
        if (me && E::TryGetActorLocation(me, at)) {
            const FVector fwd = E::GetActorForwardVector(me);
            const FVector p{at.X + fwd.X * 150.f, at.Y + fwd.Y * 150.f, at.Z};
            void* const patty = Spawn(Load(L"prop_cookingFood_rawPatty"), p);
            void* const cheese = Spawn(Load(L"prop_food_blueBrainCheese"), FVector{p.X, p.Y + 60.f, p.Z});
            UE_LOGI("[SUBCLASS] host: spawned a raw patty (%d) and a blue brain cheese (%d)", patty ? 1 : 0, cheese ? 1 : 0);
        }
    }
    if (actor && g_step == 0 && t >= 80000) {
        g_step = 1;
        void* patty = nullptr;
        for (void* p : All(L"prop_cookingFood_rawPatty_C"))
            if (Eid(p)) patty = p;
        FVector at{};
        if (patty) E::TryGetActorLocation(patty, at);
        const bool ran = patty && ue_wrap::prop_use::CallActionOptionIndex(patty, me, nullptr, at, 4, nullptr);
        UE_LOGI("[SUBCLASS] client: pressed action 4 on raw patty eid=%u (ran=%d, live after=%d)", patty ? Eid(patty) : 0,
                ran ? 1 : 0, patty && R::IsLive(patty) ? 1 : 0);
    }
    if (actor && g_step == 1 && t >= 95000) {
        g_step = 2;
        const bool loaded = Load(L"prop_funGun_rpg_rapid") != nullptr;
        // Its own list_props row, from its class default: the plain RPG's row would make the plain gun.
        void* const cdo = R::FindClassDefaultObject(L"prop_funGun_rpg_rapid_C");
        const std::wstring row = cdo ? R::ToString(ue_wrap::prop::GetPropName(cdo)) : std::wstring(L"fungun_rpg");
        UE_LOGI("[SUBCLASS] client: the rapid RPG (row '%ls') into the hand (class %d, taken %d)", row.c_str(),
                loaded ? 1 : 0, coop::dev::drill_hand::TakeIntoHandNow(row.c_str(), L"prop_funGun_rpg_rapid_C") ? 1 : 0);
    }
    if (actor && g_step == 2 && t >= 97000) {
        if (!coop::hand_item::OwnAnnouncedA(L"prop_funGun_rpg_C")) {
            if (t >= 110000) {
                g_step = 3;
                UE_LOGW("[SUBCLASS] client: the rapid RPG was never announced");
            }
        } else {
            g_step = 3;
            E::MainPlayerGrabState gs{};
            void* const held = me && E::ReadMainPlayerGrabState(me, gs) ? gs.holdingActor : nullptr;
            int32_t* const ammo = IntField(held, L"ammo_primary");
            int32_t* const mag = IntField(held, L"maxMagazine_primary");
            if (ammo && mag) *ammo = *mag;
            UE_LOGI("[SUBCLASS] client: holding %ls, ammo %d, fired (LMB ran=%d)",
                    held ? R::ClassNameOf(held).c_str() : L"none", ammo ? *ammo : -1,
                    CallHandVerb(held, me, L"playerHandUse_LMB") ? 1 : 0);
        }
    }
    if (host && g_step == 1 && t >= 150000) {
        g_step = 2;
        UE_LOGI("[SUBCLASS] DONE");
    }
    if (t < 55000 || t > 160000 || Now() < g_reportAt) return;
    g_reportAt = Now() + 5000;
    Report(host ? "host" : "client");
}

}  // namespace coop::dev::subclass_probe
