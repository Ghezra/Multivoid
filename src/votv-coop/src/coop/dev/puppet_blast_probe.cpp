// coop/dev/puppet_blast_probe.cpp -- [dev] a blast at a peer's puppet on the rig (VOTVCOOP_PUPPET_BLAST_PROBE=1).
//
// CLIENT, 10 s after it is connected: moves its own player to level ground about 29 m from the host's spot
// (-750, 3460, 6240). HOST, 30 s after a client is connected: spawns an explosion_C (damage 60, radius 300 cm) at the
// client's puppet, as a blast the client set off beside itself is spawned. BOTH, every second from 25 s to 45 s: this
// machine's own health and dead flag (`[BLAST] me`); the HOST also the puppet's dead flag and the damage the lane refused
// on puppets (`[BLAST] puppet`). Before the puppet guards a hit on the puppet came off the host's own health.

#include "coop/dev/puppet_blast_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/player_damage.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/world/explosion.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace coop::dev::puppet_blast_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
namespace X = ue_wrap::explosion;
namespace V = ue_wrap::vitals;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_PUPPET_BLAST_PROBE") == "1";
    return on;
}

int Dead(void* actor) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!actor || !R::FindBoolProperty(R::ClassOf(actor), L"dead", off, mask)) return -1;
    return (static_cast<const uint8_t*>(actor)[off] & mask) ? 1 : 0;
}

void* Puppet() {
    for (uint8_t slot = 1; slot < coop::net::kMaxPeers; ++slot) {
        coop::RemotePlayer* p = coop::players::Registry::Get().Puppet(slot);
        if (p && p->valid() && p->actor()) return p->actor();
    }
    return nullptr;
}

bool Blast(void* puppet) {
    ue_wrap::FVector at{};
    if (!puppet || !E::TryGetActorLocation(puppet, at) || !X::ResolveNow(X::Cls::Explosion)) return false;
    const float loc[3] = {at.X, at.Y, at.Z}, rot[3] = {0.f, 0.f, 0.f}, scale[3] = {1.f, 1.f, 1.f};
    const X::Params p{60.f, 300.f, 1000.f, 0.f, 0.f, 0.f, 0.f, 0, true, false};
    return X::SpawnDeferred(X::Cls::Explosion, loc, rot, scale, p, nullptr) != nullptr;
}

uint64_t g_startAt = 0, g_reportAt = 0;
bool g_moved = false, g_blasted = false;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[BLAST] probe armed role=%s", host ? "host" : "client");
    }
    const uint64_t t = Now() - g_startAt;
    if (!host && !g_moved && t >= 10000u) {
        g_moved = true;
        void* me = coop::players::Registry::Get().Local();
        UE_LOGI("[BLAST] client: moved away (ok=%d)", me && E::SetActorLocation(me, {-750.f, 3460.f, 6240.f}) ? 1 : 0);
    }
    if (host && !g_blasted && t >= 30000u) {
        g_blasted = true;
        void* puppet = Puppet();
        ue_wrap::FVector a{}, b{};
        void* me = coop::players::Registry::Get().Local();
        const bool seen = puppet && me && E::TryGetActorLocation(puppet, a) && E::TryGetActorLocation(me, b);
        const float d = seen ? std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z)) : -1.f;
        UE_LOGI("[BLAST] host: a blast at the client's puppet, %.0f cm from the host (ok=%d)", d, Blast(puppet) ? 1 : 0);
    }
    if (t < 25000u || t > 45000u || Now() < g_reportAt) return;
    g_reportAt = Now() + 1000;
    float health = -1.f;
    V::Read(V::Field::Health, &health);
    void* me = coop::players::Registry::Get().Local();
    UE_LOGI("[BLAST] me health=%.1f dead=%d", health, Dead(me));
    if (host) UE_LOGI("[BLAST] puppet dead=%d refused=%u", Dead(Puppet()), coop::player_damage::RefusedDamage());
}

}  // namespace coop::dev::puppet_blast_probe
