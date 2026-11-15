// coop/dev/deathhand_probe.cpp -- [dev] a player dies with an item in its hand and is revived
// (VOTVCOOP_DEATHHAND_PROBE=host|client names who dies; VOTVCOOP_DEATHHAND_NODEATH=1 the control: the hand
// emptied and a toolbox put down at its feet instead of the death).
//
// In a session a death's run-ending travel is cancelled and the player revived in place (coop/player/death_revive),
// where single player reloads the save. THE DYING PLAYER: at 20 s a toolbox into its hand (the hand is the inventory's
// equipment record, shown by the player's holding_actor), at 25 s the game's own Add Player Damage, twice its max health.
// Every second from 20 s to 60 s: dead, isRagdoll, the hand's record name, holding_actor (its class and whether it is
// live) and grabbing_actor (`[DEATHHAND]`).
// THE DYING PLAYER, at 48 s: stands beside the toolbox nearest where it died; at 50 s puts it into its inventory
// (putObjectInventory2, where every collect key ends), and at 53 s counts
// the toolboxes in its inventory and in the world.

#include "coop/dev/deathhand_probe.h"

#include "coop/config/config.h"
#include "coop/dev/drill_hand.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

namespace coop::dev::deathhand_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Dier() {
    static const std::string f = coop::config::ReadEnv("VOTVCOOP_DEATHHAND_PROBE");
    return f;
}

int Bool(void* o, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!o || !R::FindBoolProperty(R::ClassOf(o), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(o)[off] & mask) ? 1 : 0;
}

std::wstring Describe(void* a) {
    if (!a) return L"none";
    if (!R::IsLive(a)) return L"dead-object";
    return R::ClassNameOf(a);
}

void Log(void* me, const char* when) {
    E::MainPlayerGrabState gs{};
    E::ReadMainPlayerGrabState(me, gs);
    UE_LOGI("[DEATHHAND] %s: dead %d isRagdoll %d | hand record '%ls' | holding_actor %ls | grabbing_actor %ls", when,
            Bool(me, L"dead"), Bool(me, L"isRagdoll"), ue_wrap::inventory::ReadHeldItemName().c_str(),
            Describe(gs.holdingActor).c_str(), Describe(gs.grabbingActor).c_str());
}

bool NoDeath() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_DEATHHAND_NODEATH") == "1";
    return v;
}

void* NearestToolbox(const ue_wrap::FVector& at) {
    void* best = nullptr;
    float bestD = 1e30f;
    for (void* o : R::FindObjectsByClass(L"prop_toolbox_C")) {
        ue_wrap::FVector p{};
        if (!o || !R::IsLive(o) || R::NameStartsWith(R::NameOf(o), L"Default__") || !E::TryGetActorLocation(o, p)) continue;
        const float d = (p.X - at.X) * (p.X - at.X) + (p.Y - at.Y) * (p.Y - at.Y) + (p.Z - at.Z) * (p.Z - at.Z);
        if (d < bestD) {
            bestD = d;
            best = o;
        }
    }
    return best;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;
ue_wrap::FVector g_diedAt{};
void* g_box = nullptr;

}  // namespace

void Tick(coop::net::Session* session) {
    if (Dier().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if ((Dier() == "host") != host) return;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[DEATHHAND] probe armed role=%s", host ? "host" : "client");
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    if (g_step == 0 && t >= 20000u) {
        g_step = 1;
        UE_LOGI("[DEATHHAND] a toolbox into the hand (ok=%d)",
                coop::dev::drill_hand::TakeIntoHandNow(L"toolbox", L"prop_toolbox_C") ? 1 : 0);
    }
    if (g_step == 1 && t >= 25000u) {
        g_step = 2;
        E::TryGetActorLocation(me, g_diedAt);
        Log(me, "before the death");
        if (NoDeath()) {
            // The control: the hand emptied and a toolbox put down at the player's feet, as the death's drop leaves it.
            const bool emptied = coop::dev::drill_hand::TakeIntoHandNow(L"None", nullptr);
            void* const cls = R::FindClass(L"prop_toolbox_C");
            void* const box = cls ? E::SpawnActor(cls, {g_diedAt.X, g_diedAt.Y, g_diedAt.Z - 40.f}) : nullptr;
            UE_LOGI("[DEATHHAND] control: hand emptied (ok=%d), a toolbox put down (ok=%d)", emptied ? 1 : 0, box ? 1 : 0);
        } else {
            UE_LOGI("[DEATHHAND] the lethal hit (ok=%d)", E::InvokeAddPlayerDamage(me, 400.f, true) ? 1 : 0);
        }
    }
    if (g_step == 2 && t >= 48000u && !g_box) {
        // Beside the dropped toolbox, the one nearest where the player stood when it left the hand.
        g_box = NearestToolbox(g_diedAt);
        ue_wrap::FVector at{};
        if (g_box && E::TryGetActorLocation(g_box, at))
            UE_LOGI("[DEATHHAND] beside the dropped toolbox (moved=%d)",
                    E::SetActorLocation(me, {at.X + 80.f, at.Y, at.Z + 60.f}) ? 1 : 0);
    }
    if (g_step == 2 && t >= 50000u) {
        g_step = 3;
        Log(me, "after the revive");
        // The toolbox the death dropped, collected as the use key's collect does: looked at, then collectObject.
        void* const box = g_box && R::IsLive(g_box) ? g_box : nullptr;
        // putObjectInventory2(actor): where every collect key ends (the drop key's, the use key's).
        bool ran = false, put = false;
        if (box) {
            ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(me), L"putObjectInventory2"));
            if (f.valid() && f.Set<void*>(L"inputPin", box) && f.Set(L"nonotify", false) && ue_wrap::Call(me, f)) {
                ran = true;
                put = f.Get<bool>(L"return");
            }
        }
        UE_LOGI("[DEATHHAND] putObjectInventory2 on the dropped toolbox (found=%d ran=%d put=%d)", box ? 1 : 0, ran ? 1 : 0,
                put ? 1 : 0);
    }
    if (g_step == 3 && t >= 53000u) {
        g_step = 4;
        int carried = 0, live = 0;
        ue_wrap::inventory::LivePersonalStore store;
        if (ue_wrap::inventory::ReadLivePersonalStore(store))
            for (const auto& r : store.records)
                if (r.className == L"prop_toolbox_C") ++carried;
        for (void* o : R::FindObjectsByClass(L"prop_toolbox_C"))
            if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) ++live;
        UE_LOGI("[DEATHHAND] after the collect: toolboxes in the inventory %d, in the world %d", carried, live);
        Log(me, "after the collect");
    }
    if (t >= 20000u && t < 60000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 1000;
        char when[32];
        std::snprintf(when, sizeof(when), "t=%llu", t / 1000ull);
        Log(me, when);
    }
}

}  // namespace coop::dev::deathhand_probe
