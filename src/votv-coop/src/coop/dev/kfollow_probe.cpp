// coop/dev/kfollow_probe.cpp -- [dev] an Omega following a player (VOTVCOOP_KFOLLOW_PROBE=client|host, the follower, on
// both peers), for Ghezra/Multivoid#38: on a client the following Omega was seen to jump instead of walking.
//
// BOTH PEERS, from 20 s after a client is connected to 75 s: each frame, an Omega that moved more than 25 cm since the
// last frame is logged (`[KFOLLOW] leap`), and every 2 s its place, speed and distance to the follower. THE FOLLOWER at
// 20 s gives the nearest Omega "follow" through its own actionName, and from 24 s walks along X, turning every 4 s; at 40 s it goes to a NavMesh point 40 m off.

#include "coop/dev/kfollow_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/kerfur.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_nav.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace coop::dev::kfollow_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace K = ue_wrap::kerfur;
namespace E = ue_wrap::engine;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Who() {
    static const std::string v = coop::config::ReadEnv("VOTVCOOP_KFOLLOW_PROBE");
    return v;
}

uint64_t g_startAt = 0;
uint64_t g_lastAt = 0;
uint64_t g_nextLog = 0;
bool g_told = false;
bool g_scanned = false;
bool g_moved = false;
int g_leaps = 0;
int g_frames = 0;
std::unordered_map<void*, ue_wrap::FVector> g_last;

void* NearestOmega(void* me) {
    ue_wrap::FVector at{};
    if (!E::TryGetActorLocation(me, at)) return nullptr;
    void* best = nullptr;
    float bestD = 1e12f;
    for (void* k : K::AllKerfurs()) {
        ue_wrap::FVector kl{};
        if (!E::TryGetActorLocation(k, kl)) continue;
        const float d = (kl.X - at.X) * (kl.X - at.X) + (kl.Y - at.Y) * (kl.Y - at.Y);
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Who().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[KFOLLOW] probe armed role=%s follower=%s", host ? "host" : "client", Who().c_str());
    }
    const uint64_t t = Now() - g_startAt;
    if (t < 20000u) return;
    if (host && !g_scanned) {
        // Where the NavMesh reaches: a point at each distance along each axis, projected onto it.
        g_scanned = true;
        void* const hm = coop::players::Registry::Get().Local();
        ue_wrap::FVector o{};
        if (hm && E::TryGetActorLocation(hm, o)) {
            const float dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (float dist : {1000.f, 3000.f, 6000.f, 12000.f, 25000.f}) {
                std::string line;
                for (const auto& d : dirs) {
                    ue_wrap::FVector got{};
                    const bool ok = E::ProjectToNav(hm, {o.X + d[0] * dist, o.Y + d[1] * dist, o.Z},
                                                    {300.f, 300.f, 20000.f}, &got);
                    char b[64];
                    std::snprintf(b, sizeof(b), " %s", ok ? "nav" : "-");
                    line += b;
                }
                UE_LOGI("[KFOLLOW] navmesh at %.0f cm (+X -X +Y -Y):%s", dist, line.c_str());
            }
        }
    }
    const uint64_t dtMs = g_lastAt ? Now() - g_lastAt : 0;
    g_lastAt = Now();
    if (t >= 75000u) {
        if (g_frames >= 0) {
            UE_LOGI("[KFOLLOW] done: %d leap(s) in %d frame(s)", g_leaps, g_frames);
            g_frames = -1;
        }
        return;
    }
    ++g_frames;
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector ml{};
    if (me) E::TryGetActorLocation(me, ml);
    const bool logNow = Now() >= g_nextLog;
    if (logNow) g_nextLog = Now() + 2000;
    for (void* k : K::AllKerfurs()) {
        ue_wrap::FVector kl{};
        if (!E::TryGetActorLocation(k, kl)) continue;
        auto it = g_last.find(k);
        if (it != g_last.end()) {
            const float dx = kl.X - it->second.X, dy = kl.Y - it->second.Y, dz = kl.Z - it->second.Z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > 25.f) {
                ++g_leaps;
                UE_LOGI("[KFOLLOW] leap %.0f cm in %llu ms (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f) t=%llu", d,
                        static_cast<unsigned long long>(dtMs), it->second.X, it->second.Y, it->second.Z, kl.X, kl.Y,
                        kl.Z, static_cast<unsigned long long>(t));
            }
        }
        g_last[k] = kl;
        if (logNow) {
            const auto v = E::GetActorVelocity(k);
            UE_LOGI("[KFOLLOW] at (%.0f,%.0f,%.0f) speed %.0f, %.0f cm from me (%.0f,%.0f) t=%llu", kl.X, kl.Y, kl.Z,
                    std::sqrt(v.X * v.X + v.Y * v.Y), std::sqrt((kl.X - ml.X) * (kl.X - ml.X) + (kl.Y - ml.Y) * (kl.Y - ml.Y)),
                    ml.X, ml.Y, static_cast<unsigned long long>(t));
        }
    }
    if (Who() != (host ? "host" : "client") || !me) return;
    if (!g_told) {
        g_told = true;
        void* const omega = NearestOmega(me);
        UE_LOGI("[KFOLLOW] follower: follow ran=%d", omega && K::RunActionName(omega, me, L"follow") ? 1 : 0);
    }
    if (t >= 40000u && !g_moved) {
        // Far off: a NavMesh point 40 m away, through the player's own teleportWObackrooms.
        g_moved = true;
        const float dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : dirs) {
            ue_wrap::FVector got{};
            if (!E::ProjectToNav(me, {ml.X + d[0] * 4000.f, ml.Y + d[1] * 4000.f, ml.Z}, {500.f, 500.f, 20000.f}, &got))
                continue;
            void* const f = R::FindFunction(R::ClassOf(me), L"teleportWObackrooms");
            ue_wrap::ParamFrame pf(f);
            ue_wrap::FTransform tr{};
            tr.RotW = 1.f;
            tr.TX = got.X; tr.TY = got.Y; tr.TZ = got.Z + 100.f;
            tr.SX = tr.SY = tr.SZ = 1.f;
            const bool ran = f && pf.valid() && pf.Set<ue_wrap::FTransform>(L"newTransform", tr) &&
                             pf.Set<bool>(L"useRotation", false) && ue_wrap::Call(me, pf);
            UE_LOGI("[KFOLLOW] follower: to (%.0f,%.0f,%.0f), ran=%d", got.X, got.Y, got.Z, ran ? 1 : 0);
            break;
        }
    }
    if (t >= 24000u && t < 40000u) {
        const float dir = ((t - 24000u) / 4000u) % 2 == 0 ? 1.f : -1.f;
        E::AddMovementInput(me, ue_wrap::FVector{dir, 0.f, 0.f}, 1.f, true);
    }
}

}  // namespace coop::dev::kfollow_probe
