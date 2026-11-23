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
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
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

bool Armed() {
    static const bool v = !coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE").empty();
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

void HostStep(uint64_t now, uint64_t t) {
    if (g_spawned && now - g_spawnedAt >= 12000u) {
        const bool live = R::IsLive(g_spawned);
        UE_LOGI("[ENTITY] host: destroy %ls (live=%d ok=%d)", R::ClassNameOf(g_spawned).c_str(), live ? 1 : 0,
                live && E::DestroyActor(g_spawned) ? 1 : 0);
        g_spawned = nullptr;
    }
    if (g_next >= kCount || t < 35000u + static_cast<uint64_t>(g_next) * 20000u) return;
    const wchar_t* const name = kClasses[g_next++];
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
