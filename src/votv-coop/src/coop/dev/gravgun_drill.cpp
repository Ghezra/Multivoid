// coop/dev/gravgun_drill.cpp -- [dev] how many gravity gun components shoot for one click, on three peers
// (VOTVCOOP_GRAVGUN_DRILL=1 on every peer; red arm week6_red=gravgun_local). Times are from the host's connect.
//   HOST, 30 s: a prop_gravgun_C spawned and taken into its hand. 36 s, 42 s, 48 s: two clicks 0.3 s apart, inside one
//   shot's recharge -- its player's input_fire set, as the fire key does -- and for 2 s each comp_gravitygun_C on this
//   machine whose `recharge` jumps is counted, its owner this machine's player or a puppet.

#include "coop/dev/gravgun_drill.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_gravgun.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace coop::dev::gravgun_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

bool Armed() {
    static const bool v = !coop::config::ReadEnv("VOTVCOOP_GRAVGUN_DRILL").empty();
    return v;
}

uint64_t g_startAt = 0;
int g_step = 0, g_clicks = 0;
uint64_t g_clickAt = 0;
std::unordered_map<void*, float> g_last;   // each component's recharge as last seen
std::unordered_map<void*, bool> g_shot;    // the components that shot this click

float Recharge(void* comp) {
    const int32_t off = R::FindPropertyOffset(R::ClassOf(comp), L"recharge");
    float v = 0.f;
    if (off >= 0) std::memcpy(&v, static_cast<const uint8_t*>(comp) + off, sizeof(v));
    return v;
}

bool SetFire(void* me) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!R::FindBoolProperty(R::ClassOf(me), L"input_fire", off, mask) || off < 0) return false;
    static_cast<uint8_t*>(me)[off] |= mask;
    return true;
}

std::vector<void*> Components() {
    std::vector<void*> out;
    for (void* o : R::FindObjectsByClass(L"comp_gravitygun_C"))
        if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) out.push_back(o);
    return out;
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!Armed() || !session || !session->running() || !session->connected() ||
        session->role() != coop::net::Role::Host)
        return;
    const uint64_t now = ::GetTickCount64();
    if (!g_startAt) {
        g_startAt = now;
        UE_LOGI("[GRAVGUN] drill armed");
    }
    const uint64_t t = now - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    if (g_step == 0 && t >= 30000u) {
        g_step = 1;
        void* const cls = ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/prop_gravgun.prop_gravgun_C");
        ue_wrap::FVector at{};
        E::TryGetActorLocation(me, at);
        void* const gun = cls ? E::SpawnActor(cls, {at.X, at.Y, at.Z + 60.f}) : nullptr;
        bool collected = false;
        const bool held = gun && E::CallMainPlayerHoldObject(me, gun, collected);
        UE_LOGI("[GRAVGUN] host: the gravity gun in its hand (spawned=%d held=%d), %zu gun components here",
                gun ? 1 : 0, held ? 1 : 0, Components().size());
    }
    // A click: the grab set, then 2 s of watching every component's recharge.
    if (g_step == 1 && t >= 36000u + static_cast<uint64_t>(g_clicks) * 6000u && g_clicks < 3) {
        g_step = 2;
        g_clickAt = now;
        g_shot.clear();
        g_last.clear();
        for (void* c : Components()) g_last[c] = Recharge(c);
        UE_LOGI("[GRAVGUN] host: clicks %d (fire set=%d)", g_clicks + 1, SetFire(me) ? 1 : 0);
    }
    static bool s_second = false;
    if (g_step == 2 && !s_second && now - g_clickAt >= 300u) {
        s_second = true;
        SetFire(me);
    }
    if (g_step == 2) {
        for (void* c : Components()) {
            const float r = Recharge(c);
            auto it = g_last.find(c);
            if (it != g_last.end() && r > it->second + 0.25f) g_shot[c] = true;
            g_last[c] = r;
        }
        if (now - g_clickAt >= 2000u) {
            int local = 0, puppet = 0;
            for (const auto& kv : g_shot) {
                void* const owner = R::OuterOf(kv.first);
                if (owner == me) ++local;
                else ++puppet;
            }
            s_second = false;
            ++g_clicks;
            g_step = 1;
            UE_LOGI("[GRAVGUN] click %d: %d component(s) shot -- this player's %d, puppets' %d (%zu components, %u "
                    "puppet ticks refused)", g_clicks, local + puppet, local, puppet, Components().size(),
                    coop::puppet_gravgun::RefusedTicks());
        }
    }
    if (g_clicks >= 3 && g_step == 1) {
        g_step = 3;
        UE_LOGI("[GRAVGUN] done");
    }
}

}  // namespace coop::dev::gravgun_drill
