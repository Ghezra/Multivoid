// coop/dev/entity_probe.cpp -- [dev] the wiki's extraterrestrials on every peer (VOTVCOOP_ENTITY_PROBE=1 on every
// peer). HOST: from 35 s, every 20 s the next class of the list is loaded and spawned 30 m before its player, and
// destroyed 12 s on (`[ENTITY] host: spawn`, `destroy`). Every peer, each 4 s (`[ENTITY]`): its live count of each
// class of the list that has one, so a class the host spawned and a client never counted, or kept, is seen.
// VOTVCOOP_ENTITY_PROBE=real: the game's own spawns instead -- 35 s the super fog as its event spawns it (its own
// spawnUfo mints the Thunder Wisp), 75 s the signal screen's spawn_trifo (the TriFO).
// VOTVCOOP_ENTITY_PROBE=anom: the wiki's Anomalous list instead. Every peer, each 10 s from 30 s (`[ANOM]`): each
// class's count and points (10 m), and the classes owning an EMF, photographic or paranormal part; the host spawns
// and destroys the list's classes as above, but not the Killer Birch nor the Red Void Orb (a raw birch_C or
// npc_orborb_C ends the game, wanting its spawner's stamp). Every peer, at 45 s: the wanderers' own rolls (the log's
// and the sheet ghost's Teleport, the booth's Timer, William's lookPlace), the gamemode's tickerBody and tickerFunguy
// and the Mimic's gen, many times, as each machine's own timers would.

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
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <map>
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

bool Anom() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ENTITY_PROBE") == "anom";
    return v;
}
const wchar_t* const* List() { return Anom() ? kAnom : kClasses; }
size_t ListCount() { return Anom() ? sizeof(kAnom) / sizeof(kAnom[0]) : sizeof(kClasses) / sizeof(kClasses[0]); }

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
