// coop/dev/rejoininv_probe.cpp -- [dev] every peer's own inventory across a client's leave and rejoin
// (VOTVCOOP_REJOININV_PROBE=1 on every peer; VOTVCOOP_REJOININV_OWN_ROW names the client's own item's catalog row,
// default crowbar).
//
// A client's clock starts once its world is announced and its join is over, and starts again at each new connect (a
// rejoin in the same process as much as a fresh one); the host's starts at its first connect and runs on. HOST, once a
// process, at 15 s: spawns a plush (prop_eriePlush_C) and a lighter (prop_lighter_C) 80 cm before itself, at 17 s and
// 18 s takes each into its own inventory through the player's collectObject and logs their keys. CLIENT, at 15 s, only
// when its store holds no item of the own row's class: looks 35 degrees down and spawns the row through the spawn menu
// (coop/dev/menu_spawn_drive), and at 17 s takes it in the same way, so a client in its first life holds an item of its
// own to tell from the host's. EVERY PEER, at 15 s and every 10 s after (600 s at most): its life (connects seen in this
// process), its store's record count, its hand's class, each record's class and key, and how many records are of the
// host's two classes (`[REJOININV]`). HOST, every second: a slot's connect and leave, with that slot's join count; 40 s
// after a slot's second join, a line that the slot's rejoin is logged. CLIENT, 40 s into each life: a line that the
// life is logged.

#include "coop/dev/rejoininv_probe.h"

#include "coop/config/config.h"
#include "coop/dev/menu_spawn_drive.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/session/join_progress.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace coop::dev::rejoininv_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
namespace MS = coop::dev::menu_spawn_drive;
using ue_wrap::FVector;

struct Item { const wchar_t* cls; const wchar_t* path; };
constexpr Item kHostItems[2] = {
    {L"prop_eriePlush_C", L"/Game/objects/prop_eriePlush.prop_eriePlush_C"},
    {L"prop_lighter_C", L"/Game/objects/prop_lighter.prop_lighter_C"},
};

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_REJOININV_PROBE") == "1";
    return v;
}
const std::wstring& OwnRow() {
    static const std::wstring r = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_REJOININV_OWN_ROW");
        return v.empty() ? std::wstring(L"crowbar") : std::wstring(v.begin(), v.end());
    }();
    return r;
}

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off < 0 ? nullptr : *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(obj) + off);
}

std::vector<void*> Instances(void* cls) {
    std::vector<void*> out;
    if (cls)
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

// The player's own take of `item` into its inventory: lookAtActor and lookAtComponent set, then collectObject.
bool Collect(void* me, void* item) {
    if (!me || !item || !R::IsLive(item)) return false;
    const int32_t laOff = R::FindPropertyOffset(R::ClassOf(me), L"lookAtActor");
    const int32_t lcOff = R::FindPropertyOffset(R::ClassOf(me), L"lookAtComponent");
    if (laOff >= 0) *reinterpret_cast<void**>(static_cast<char*>(me) + laOff) = item;
    if (lcOff >= 0) *reinterpret_cast<void**>(static_cast<char*>(me) + lcOff) = Field(item, L"StaticMesh");
    return R::CallFunction(me, R::FindDispatchFunctionCached(R::ClassOf(me), L"collectObject"), nullptr);
}

bool ClientReady() {
    void* const me = coop::players::Registry::Get().Local();
    return me && R::IsLive(me) && coop::net_pump::HasAnnouncedWorldReady() &&
           coop::join_progress::CurrentPhase() == coop::join_progress::Phase::Idle;
}

std::wstring g_ownClass;   // the client's own row's class name, once read

void Report(bool host, int life, uint64_t t) {
    ue_wrap::inventory::LivePersonalStore store;
    const bool read = ue_wrap::inventory::ReadLivePersonalStore(store);
    void* const me = coop::players::Registry::Get().Local();
    void* const hand = Field(me, L"holding_actor");
    int hostItems = 0, own = 0;
    std::map<std::wstring, int> perClass;
    for (const auto& r : store.records) {
        ++perClass[r.className];
        for (const Item& h : kHostItems) hostItems += r.className == h.cls ? 1 : 0;
        own += !g_ownClass.empty() && r.className == g_ownClass ? 1 : 0;
    }
    std::wstring classes;
    for (const auto& kv : perClass) classes += kv.first + L" x" + std::to_wstring(kv.second) + L"; ";
    UE_LOGI("[REJOININV] %hs slot=%u life=%d t=%llu store=%hs records=%zu hand=%ls hostClassRecords=%d ownRowRecords=%d "
            "classes: %ls", host ? "host" : "client", static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId()),
            life, t / 1000ull, read ? "read" : "UNREAD", store.records.size(),
            hand && R::IsLive(hand) ? R::ClassNameOf(hand).c_str() : L"-", hostItems, own, classes.c_str());
    for (size_t i = 0; i < store.records.size() && i < 24; ++i)
        UE_LOGI("[REJOININV]   record %zu class=%ls key=%ls", i, store.records[i].className.c_str(),
                store.records[i].key.c_str());
}

uint64_t g_startAt = 0, g_nextReport = 0, g_slotPollAt = 0;
int g_life = 0;
bool g_wasConnected = false, g_lifeSaid = false;
// HOST (once a process): 0 none, 1 spawned, 2 first taken, 3 both taken.
int g_hostStep = 0;
ue_wrap::CachedObjRef g_hostItem[2];
// CLIENT (each life): 0 none, 1 spawned, 2 taken or skipped.
int g_clientStep = 0;
ue_wrap::CachedObjRef g_ownItem;
// HOST: each slot's connect state and join count, and when its latest join began.
bool g_slotOn[coop::players::kMaxPeers] = {};
int g_slotJoins[coop::players::kMaxPeers] = {};
uint64_t g_slotJoinAt[coop::players::kMaxPeers] = {};
bool g_slotRejoinSaid[coop::players::kMaxPeers] = {};

void HostSlots(coop::net::Session* s) {
    if (Now() < g_slotPollAt) return;
    g_slotPollAt = Now() + 1000;
    for (int sl = 1; sl < coop::players::kMaxPeers; ++sl) {
        const bool on = s->IsSlotConnected(sl);
        if (on != g_slotOn[sl]) {
            g_slotOn[sl] = on;
            if (on) {
                ++g_slotJoins[sl];
                g_slotJoinAt[sl] = Now();
                g_slotRejoinSaid[sl] = false;
            }
            UE_LOGI("[REJOININV] host: slot %d %hs (joins %d)", sl, on ? "connected" : "LEFT", g_slotJoins[sl]);
        }
        if (on && g_slotJoins[sl] >= 2 && !g_slotRejoinSaid[sl] && Now() - g_slotJoinAt[sl] >= 40000u) {
            g_slotRejoinSaid[sl] = true;
            UE_LOGI("[REJOININV] host: slot %d rejoin logged", sl);
        }
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const bool connected = session->connected();
    if (host) HostSlots(session);   // polled while no client is connected too, so a lone client's leave is seen
    if (!connected) {
        if (g_wasConnected && !host) {   // a client's leave: the next connect is a new life
            UE_LOGI("[REJOININV] client: disconnected, life %d over", g_life);
            g_startAt = 0;
        }
        g_wasConnected = false;
        return;
    }
    if (!g_wasConnected) {
        g_wasConnected = true;
        if (!host || g_life == 0) {
            ++g_life;
            g_startAt = 0;
        }
    }
    if (!g_startAt) {
        if (!host && !ClientReady()) return;
        g_startAt = Now();
        g_nextReport = g_startAt + 15000u;
        g_lifeSaid = false;
        g_clientStep = 0;
        g_ownItem.Reset();
        UE_LOGI("[REJOININV] probe armed role=%s slot=%u life=%d", host ? "host" : "client",
                static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId()), g_life);
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();

    if (host && g_hostStep == 0 && t >= 15000u && me) {
        g_hostStep = 1;
        FVector at{};
        E::TryGetActorLocation(me, at);
        const FVector f = E::GetActorForwardVector(me);
        for (int i = 0; i < 2; ++i) {
            void* cls = OI::ClassByName(kHostItems[i].cls);
            if (!cls) cls = ue_wrap::asset_load::LoadObjectByPath(kHostItems[i].path);
            void* const a = cls ? E::SpawnActor(cls, {at.X + f.X * 80.f - f.Y * (30.f * i), at.Y + f.Y * 80.f + f.X * (30.f * i),
                                                      at.Z + 20.f})
                                : nullptr;
            g_hostItem[i].Set(a);
            UE_LOGI("[REJOININV] host: spawned %ls (ok=%d)", kHostItems[i].cls, a ? 1 : 0);
        }
    }
    if (host && (g_hostStep == 1 || g_hostStep == 2) && t >= 17000u + 1000u * (g_hostStep - 1)) {
        const int i = g_hostStep - 1;
        ++g_hostStep;
        void* const item = g_hostItem[i].Get();
        const std::wstring key = item ? ue_wrap::prop::GetInteractableKeyString(item) : L"";
        const bool ran = Collect(me, item);
        UE_LOGI("[REJOININV] host: put %ls key=%ls into its inventory (collectObject ran=%d)", kHostItems[i].cls,
                key.c_str(), ran ? 1 : 0);
    }

    if (!host && g_clientStep == 0 && t >= 15000u && me) {
        g_clientStep = 1;
        if (g_ownClass.empty())
            if (void* const cls = MS::RowClass(OwnRow())) g_ownClass = R::ToString(R::NameOf(cls));
        ue_wrap::inventory::LivePersonalStore store;
        int held = 0;
        if (ue_wrap::inventory::ReadLivePersonalStore(store))
            for (const auto& r : store.records) held += !g_ownClass.empty() && r.className == g_ownClass ? 1 : 0;
        if (held > 0 || g_ownClass.empty()) {
            g_clientStep = 2;
            UE_LOGI("[REJOININV] client: no own item spawned (own class %ls, already held %d)",
                    g_ownClass.empty() ? L"unresolved" : g_ownClass.c_str(), held);
        } else {
            if (void* ctrl = E::GetController(me)) {
                ue_wrap::FRotator view = E::GetControlRotation(ctrl);
                view.Pitch = -35.f;
                E::SetControlRotation(ctrl, view);
            }
            void* const cls = OI::ClassByName(g_ownClass.c_str());
            const std::vector<void*> before = Instances(cls);
            const bool ran = MS::Spawn(OwnRow());
            for (void* a : Instances(cls))
                if (std::find(before.begin(), before.end(), a) == before.end()) g_ownItem.Set(a);
            UE_LOGI("[REJOININV] client: spawned own row '%ls' through the spawn menu (ran=%d born=%d key=%ls)",
                    OwnRow().c_str(), ran ? 1 : 0, g_ownItem.Get() ? 1 : 0,
                    g_ownItem.Get() ? ue_wrap::prop::GetInteractableKeyString(g_ownItem.Get()).c_str() : L"-");
        }
    }
    if (!host && g_clientStep == 1 && t >= 17000u) {
        g_clientStep = 2;
        void* const item = g_ownItem.Get();
        const std::wstring key = item ? ue_wrap::prop::GetInteractableKeyString(item) : L"";
        const bool ran = Collect(me, item);
        UE_LOGI("[REJOININV] client: put own %ls key=%ls into its inventory (collectObject ran=%d)", g_ownClass.c_str(),
                key.c_str(), ran ? 1 : 0);
    }

    if (t <= 600000u && Now() >= g_nextReport) {
        g_nextReport = Now() + 10000u;
        Report(host, g_life, t);
    }
    if (!host && !g_lifeSaid && t >= 40000u) {
        g_lifeSaid = true;
        UE_LOGI("[REJOININV] client: life %d logged", g_life);
    }
}

}  // namespace coop::dev::rejoininv_probe
