// coop/dev/atvspawn_probe.cpp -- [dev] an ATV spawned the way the spawn menu spawns one (VOTVCOOP_ATVSPAWN_PROBE=1 on
// every peer; VOTVCOOP_ATVSPAWN_WHO=host makes the host the spawner, client slot 1 by default; VOTVCOOP_ATVSPAWN_ROW
// names the catalog row, else the first row holding "atv" whose class is of the ATV_C lineage; VOTVCOOP_ATVSPAWN_RAW=1
// spawns ATV_C itself when no row does, which is no player's path and is said so).
//
// A client's clock starts once its world is announced and its join is over, the host's at its connect.
// THE SPAWNER, client slot 1: at 24 s looks 20 degrees down, at 25 s runs the spawn menu's own spawn(FName) with the row
// (coop/dev/menu_spawn_drive), the birth a player's click makes, and logs the new ATV's key. The host as spawner waits
// until slot 1 has been ready 15 s (not before 25 s), so every joined peer is in the world for the birth.
// EVERY PEER, every 2 s from 20 s to 70 s (150 s on the host, whose clock starts before the joiner's world): the ATVs
// it holds (count), and for each its key, place, whether it is new since this peer's 20 s, the spawner's own, driven,
// the actor's bHidden, the root mesh's bVisible and bHiddenInGame, and whether it is the synced one -- on the host its
// key is announceable (the lane announces it), on a client the host's AtvSpawn named its key (`[ATVSPAWN]`).

#include "coop/dev/atvspawn_probe.h"

#include "coop/config/config.h"
#include "coop/dev/menu_spawn_drive.h"
#include "coop/interactables/atv_sync.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/session/join_progress.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace coop::dev::atvspawn_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace A  = ue_wrap::atv;
namespace MS = coop::dev::menu_spawn_drive;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ATVSPAWN_PROBE") == "1";
    return v;
}
bool HostSpawns() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ATVSPAWN_WHO") == "host";
    return v;
}
bool Raw() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_ATVSPAWN_RAW") == "1";
    return v;
}

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off < 0 ? nullptr : *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(obj) + off);
}

int Bit(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(obj)[off] & mask) ? 1 : 0;
}

std::vector<void*> Atvs() {
    std::vector<void*> out;
    A::ForEachAtv([](void* ctx, void* actor, int32_t) { static_cast<std::vector<void*>*>(ctx)->push_back(actor); }, &out);
    return out;
}

bool IsAtvClass(void* cls) {
    void* const base = ue_wrap::object_index::ClassByName(L"ATV_C");
    return cls && base && R::IsDescendantOfAny(cls, &base, 1);
}

// The row to spawn: the env's, else the first catalog row holding "atv" whose class is of the ATV lineage.
std::wstring PickRow() {
    const std::string env = coop::config::ReadEnv("VOTVCOOP_ATVSPAWN_ROW");
    if (!env.empty()) return std::wstring(env.begin(), env.end());
    const std::vector<std::wstring> rows = MS::RowsContaining(L"atv");
    std::wstring said;
    for (const std::wstring& r : rows) said += r + L" ";
    UE_LOGI("[ATVSPAWN] catalog rows holding 'atv': %ls", said.empty() ? L"(none)" : said.c_str());
    for (const std::wstring& r : rows) {
        void* const cls = MS::RowClass(r);
        UE_LOGI("[ATVSPAWN]   row '%ls' spawns %ls", r.c_str(), cls ? R::ToString(R::NameOf(cls)).c_str() : L"(no class)");
        if (IsAtvClass(cls)) return r;
    }
    return L"";
}

bool ClientReady() {
    void* const me = coop::players::Registry::Get().Local();
    return me && R::IsLive(me) && coop::net_pump::HasAnnouncedWorldReady() &&
           coop::join_progress::CurrentPhase() == coop::join_progress::Phase::Idle;
}

uint64_t g_startAt = 0, g_nextLog = 0, g_slot1ReadyAt = 0;
bool g_armedSaid = false, g_baselined = false;
int g_step = 0;   // the spawner's: 0 waiting, 1 looked down, 2 spawned
std::vector<void*> g_baseline;
ue_wrap::CachedObjRef g_mine;

void Spawn(void* me) {
    const std::vector<void*> before = Atvs();
    const std::wstring row = PickRow();
    bool ran = false;
    const char* how = "the spawn menu";
    if (!row.empty()) {
        ran = MS::Spawn(row);
    } else if (Raw()) {
        how = "a raw SpawnActor of ATV_C (NOT the spawn menu)";
        FVector at{};
        E::TryGetActorLocation(me, at);
        const FVector f = E::GetActorForwardVector(me);
        ran = E::SpawnActor(ue_wrap::object_index::ClassByName(L"ATV_C"), {at.X + f.X * 500.f, at.Y + f.Y * 500.f, at.Z + 50.f}) !=
              nullptr;
    } else {
        UE_LOGW("[ATVSPAWN] spawner: no catalog row spawns an ATV here, nothing spawned (VOTVCOOP_ATVSPAWN_ROW names one, "
                "VOTVCOOP_ATVSPAWN_RAW=1 spawns ATV_C itself)");
        return;
    }
    void* born = nullptr;
    for (void* a : Atvs())
        if (std::find(before.begin(), before.end(), a) == before.end()) born = a;
    FVector at{};
    if (born) {
        g_mine.Set(born);
        E::TryGetActorLocation(born, at);
    }
    UE_LOGI("[ATVSPAWN] spawner: spawned row '%ls' through %hs (menuReady=%d ran=%d born=%d) key=%ls at (%.0f, %.0f, %.0f)",
            row.c_str(), how, MS::Ready() ? 1 : 0, ran ? 1 : 0, born ? 1 : 0, born ? A::GetKeyString(born).c_str() : L"-",
            at.X, at.Y, at.Z);
}

void Report(bool host, uint64_t t) {
    const std::vector<void*> atvs = Atvs();
    const std::vector<std::wstring> named = host ? std::vector<std::wstring>{} : coop::atv_sync::ReceivedSpawnKeys();
    UE_LOGI("[ATVSPAWN] t=%llu %hs slot=%u atvs=%zu", t / 1000ull, host ? "host" : "client",
            static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId()), atvs.size());
    for (void* a : atvs) {
        const std::wstring key = A::GetKeyString(a);
        FVector at{};
        E::TryGetActorLocation(a, at);
        void* const mesh = Field(a, L"Mesh");
        const bool fresh = g_baselined && std::find(g_baseline.begin(), g_baseline.end(), a) == g_baseline.end();
        const bool synced = host ? coop::atv_sync::AnnounceableAtvKey(key)
                                 : std::find(named.begin(), named.end(), key) != named.end();
        UE_LOGI("[ATVSPAWN]   atv key=%ls at (%.0f, %.0f, %.0f) new=%d mine=%d driven=%d bHidden=%d mesh bVisible=%d "
                "bHiddenInGame=%d synced=%d", key.c_str(), at.X, at.Y, at.Z, fresh ? 1 : 0, g_mine.Get() == a ? 1 : 0,
                A::IsDriven(a) ? 1 : 0, Bit(a, L"bHidden"), Bit(mesh, L"bVisible"), Bit(mesh, L"bHiddenInGame"),
                synced ? 1 : 0);
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running()) return;
    if (!session->connected()) {   // a reconnect starts the clock again
        g_startAt = 0;
        return;
    }
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        if (!host && !ClientReady()) return;
        g_startAt = Now();
        if (!g_armedSaid) {
            g_armedSaid = true;
            UE_LOGI("[ATVSPAWN] probe armed role=%s slot=%u spawner=%hs", host ? "host" : "client",
                    static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId()), HostSpawns() ? "host" : "slot 1");
        }
    }
    if (!A::EnsureResolved()) return;
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    const bool spawner = HostSpawns() ? host : (!host && coop::players::Registry::Get().LocalPeerId() == 1);
    if (host && !g_slot1ReadyAt && session->IsSlotReady(1)) g_slot1ReadyAt = Now();
    if (!g_baselined && t >= 20000u) {
        g_baselined = true;
        g_baseline = Atvs();
    }
    const bool due = !host || (g_slot1ReadyAt && Now() - g_slot1ReadyAt >= 15000u);
    if (spawner && me && g_step == 0 && t >= 24000u && due) {
        g_step = 1;
        if (void* ctrl = E::GetController(me)) {
            ue_wrap::FRotator view = E::GetControlRotation(ctrl);
            view.Pitch = -20.f;
            E::SetControlRotation(ctrl, view);
        }
        g_nextLog = 0;
        return;
    }
    if (spawner && me && g_step == 1) {
        static uint64_t s_lookedAt = 0;
        if (!s_lookedAt) s_lookedAt = Now();
        if (Now() - s_lookedAt >= 1000u) {
            g_step = 2;
            Spawn(me);
        }
    }
    const uint64_t endAt = host ? 150000u : 70000u;
    if (t < 20000u || t > endAt || Now() < g_nextLog) return;
    g_nextLog = Now() + 2000;
    Report(host, t);
}

}  // namespace coop::dev::atvspawn_probe
