// coop/dev/slip_probe.cpp -- [dev] a player walks onto a soap and slips (VOTVCOOP_SLIP_PROBE=host|client names the
// slipper; VOTVCOOP_SLIP_CLASS another prop class, default prop_food_soap_C).
//
// The soap's steppedOn(player) punches the player who stepped on it (a head impulse and one at the lower legs), and a
// player's footstep (mainPlayer_C.stepOn) traces under its feet and calls steppedOn on what it finds, so a puppet's
// footsteps can call it on another machine. HOST, 20 s after a client is connected: spawns the prop 150 uu before the
// slipper, at its feet. THE SLIPPER: from 24 s walks forward for 4 s. EVERY PEER, every 500 ms from 20 s to 50 s: its
// own health, isragdoll and dead, and each puppet's (`[SLIP]`).

#include "coop/dev/slip_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_drive.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_nav.h"

#include <windows.h>

#include <cstdint>
#include <string>

namespace coop::dev::slip_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Slipper() {
    static const std::string f = coop::config::ReadEnv("VOTVCOOP_SLIP_PROBE");
    return f;
}

std::wstring PropClass() {
    const std::string c = coop::config::ReadEnv("VOTVCOOP_SLIP_CLASS");
    return c.empty() ? std::wstring(L"prop_food_soap_C") : std::wstring(c.begin(), c.end());
}

int Bool(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(obj)[off] & mask) ? 1 : 0;
}

void* SlipperActor(bool host, void* me) {
    if ((Slipper() == "host") == host) return me;
    if (!host) return nullptr;
    coop::RemotePlayer& rp = coop::puppet_drive::Puppet(1);
    return rp.valid() ? rp.GetActor() : nullptr;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;

}  // namespace

void Tick(coop::net::Session* session) {
    if (Slipper().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const bool slipper = (Slipper() == "host") == host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[SLIP] probe armed role=%s slipper=%d class=%ls", host ? "host" : "client", slipper ? 1 : 0,
                PropClass().c_str());
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    if (host && g_step == 0 && t >= 20000u) {
        void* const who = SlipperActor(true, me);
        ue_wrap::FVector at{};
        if (who && E::TryGetActorLocation(who, at)) {
            g_step = 1;
            const ue_wrap::FVector f = E::GetActorForwardVector(who);
            void* const cls = OI::ClassByName(PropClass().c_str());
            void* const p = cls ? E::SpawnActor(cls, {at.X + f.X * 150.f, at.Y + f.Y * 150.f, at.Z - 70.f}) : nullptr;
            UE_LOGI("[SLIP] host: spawned %ls before the slipper (ok=%d)", PropClass().c_str(), p ? 1 : 0);
        }
    }
    if (!host && g_step == 0 && t >= 20000u) g_step = 1;
    if (slipper && me && t >= 24000u && t < 28000u) E::AddMovementInput(me, E::GetActorForwardVector(me), 1.f, true);
    if (t >= 20000u && t < 50000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 500;
        float health = -1.f;
        ue_wrap::vitals::Read(ue_wrap::vitals::Field::Health, &health);
        coop::RemotePlayer& rp = coop::puppet_drive::Puppet(host ? 1 : 0);
        void* const pup = rp.valid() ? rp.GetActor() : nullptr;
        UE_LOGI("[SLIP] t=%llu me: health %.1f isragdoll %d dead %d | puppet isragdoll %d dead %d", t / 1000ull, health,
                Bool(me, L"isragdoll"), Bool(me, L"dead"), Bool(pup, L"isragdoll"), Bool(pup, L"dead"));
    }
}

}  // namespace coop::dev::slip_probe
