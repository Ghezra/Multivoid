// coop/dev/carry_probe.cpp -- [dev] a player picks up a prop, turns with it and lets it go; every peer logs the prop
// (VOTVCOOP_CARRY_PROBE=1; VOTVCOOP_CARRY_CLASS, default prop_microwave_C, with VOTVCOOP_CARRY_PATH its asset path when
// the class is not loaded; VOTVCOOP_CARRY_WHO=client, the default, or host names the carrier).
//
// The card: a microwave a player picked up floated and flew about the room for one player and looked right to the other.
// HOST, once slot 1's puppet stands 20 s after the host's connect: spawns one prop of the class 120 uu before the
// carrier (the puppet, or its own player for host). THE CARRIER (slot 1 for client), from 26 s: the use key's own grab
// chain on the nearest prop of the class (director AimedGrab), then for 8 s turns its view 20 degrees a second, and lets
// go through the game's own dropGrabObject. EVERY PEER, every quarter second from 20 s to 55 s after its own connect:
// the prop's place and rotation, how far it moved since the last line, whether its root simulates, and whether its own
// player holds it (`[CARRY]`).

#include "coop/dev/carry_probe.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_drive.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::carry_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_CARRY_PROBE") == "1";
    return on;
}

const std::wstring& ClassName() {
    static const std::wstring c = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_CARRY_CLASS");
        const std::string s = v.empty() ? std::string("prop_microwave_C") : v;
        return std::wstring(s.begin(), s.end());
    }();
    return c;
}

std::wstring Path() {
    const std::string v = coop::config::ReadEnv("VOTVCOOP_CARRY_PATH");
    if (!v.empty()) return std::wstring(v.begin(), v.end());
    std::wstring base = ClassName();
    if (base.size() > 2 && base.compare(base.size() - 2, 2, L"_C") == 0) base.resize(base.size() - 2);
    return L"/Game/objects/" + base + L"." + base + L"_C";
}

bool HostCarries() {
    static const bool h = coop::config::ReadEnv("VOTVCOOP_CARRY_WHO") == "host";
    return h;
}

float Dist(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::vector<void*> All() {
    std::vector<void*> out;
    if (void* const cls = OI::ClassByName(ClassName().c_str()))
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

void* Nearest(const FVector& from) {
    void* best = nullptr;
    float bestD = 1e30f;
    for (void* p : All()) {
        FVector a{};
        if (!E::TryGetActorLocation(p, a)) continue;
        const float d = Dist(a, from);
        if (d < bestD) { bestD = d; best = p; }
    }
    return best;
}

uint64_t g_startAt = 0, g_nextLog = 0, g_grabbedAt = 0;
int g_step = 0;   // 0 before the grab, 1 grabbing, 2 holding, 3 let go
void* g_prop = nullptr;
FVector g_last{};
bool g_haveLast = false;
std::unique_ptr<coop::director::AimedGrab> g_grab;

void HostSpawn(uint64_t t, void* me) {
    static bool s_done = false;
    if (s_done || t < 20000u) return;
    void* carrier = me;
    if (!HostCarries()) {
        coop::RemotePlayer& rp = coop::puppet_drive::Puppet(1);
        carrier = rp.valid() ? rp.GetActor() : nullptr;
    }
    FVector at{};
    if (!carrier || !E::TryGetActorLocation(carrier, at)) return;
    s_done = true;
    void* cls = OI::ClassByName(ClassName().c_str());
    if (!cls) cls = ue_wrap::asset_load::LoadObjectByPath(Path().c_str());
    const FVector f = E::GetActorForwardVector(carrier);
    void* const p = cls ? E::SpawnActor(cls, {at.X + f.X * 120.f, at.Y + f.Y * 120.f, at.Z}) : nullptr;
    UE_LOGI("[CARRY] host: spawned a %ls (class %p, ok=%d) 120 uu before the carrier", ClassName().c_str(), cls,
            p ? 1 : 0);
}

void CarrierTick(uint64_t t, void* me) {
    if (g_step == 0 && t >= 26000u) {
        FVector at{};
        E::TryGetActorLocation(me, at);
        g_prop = Nearest(at);
        if (!g_prop) return;
        g_grab = std::make_unique<coop::director::AimedGrab>(me, g_prop);
        g_step = 1;
    }
    if (g_step == 1) {
        const coop::director::GrabState g = g_grab->Tick();
        if (g == coop::director::GrabState::Working) return;
        UE_LOGI("[CARRY] carrier: the grab %s%s", g == coop::director::GrabState::Grabbed ? "took it" : "FAILED: ",
                g == coop::director::GrabState::Grabbed ? "" : g_grab->Why());
        g_grab.reset();
        g_step = g == coop::director::GrabState::Grabbed ? 2 : 3;
        g_grabbedAt = Now();
    }
    if (g_step == 2) {
        if (void* const pc = E::GetController(me)) {
            ue_wrap::FRotator r = E::GetControlRotation(pc);
            r.Yaw += 20.f / 60.f;
            E::SetControlRotation(pc, r);
        }
        if (Now() - g_grabbedAt >= 8000u) {
            const bool ran = R::CallFunction(me, R::FindDispatchFunctionCached(R::ClassOf(me), L"dropGrabObject"), nullptr);
            UE_LOGI("[CARRY] carrier: let go (dropGrabObject ran=%d)", ran ? 1 : 0);
            g_step = 3;
        }
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[CARRY] probe armed role=%s class=%ls carrier=%s", host ? "host" : "client", ClassName().c_str(),
                HostCarries() ? "host" : "slot 1");
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    if (host) HostSpawn(t, me);
    const bool carrier = HostCarries() ? host : (!host && coop::players::Registry::Get().LocalPeerId() == 1);
    if (carrier) CarrierTick(t, me);
    if (t < 20000u || t >= 55000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 250;
    FVector at{};
    E::TryGetActorLocation(me, at);
    void* const p = g_prop && R::IsLive(g_prop) ? g_prop : Nearest(at);
    if (!p) {
        UE_LOGI("[CARRY] t=%.2f: no %ls here", t / 1000.0, ClassName().c_str());
        return;
    }
    FVector a{};
    E::TryGetActorLocation(p, a);
    ue_wrap::FRotator r{};
    E::TryGetActorRotation(p, r);
    const float moved = g_haveLast ? Dist(a, g_last) : 0.f;
    g_last = a;
    g_haveLast = true;
    UE_LOGI("[CARRY] t=%.2f: %ls at (%.1f, %.1f, %.1f) yaw %.1f pitch %.1f roll %.1f, moved %.1f, simulating %d, held "
            "here %d, %zu of the class",
            t / 1000.0, ClassName().c_str(), a.X, a.Y, a.Z, r.Yaw, r.Pitch, r.Roll, moved,
            E::IsActorRootSimulatingPhysics(p) ? 1 : 0, coop::director::Grabbing(me) == p ? 1 : 0, All().size());
}

}  // namespace coop::dev::carry_probe
