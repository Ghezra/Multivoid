// coop/dev/binput_probe.cpp -- [dev] a client's puts from its inventory into a garbage bin, one after another
// (VOTVCOOP_BINPUT_PROBE=1).
//
// HOST, once slot 1's puppet stands 20 s after a client is connected: spawns a green garbage bin
// (prop_garbageBin_green_C) 120 uu before the puppet, unless VOTVCOOP_BINPUT_BIN=map takes the save's own nearest one
// (the client walks to it from 29 s), and three items beside it, garbage clumps (VOTVCOOP_BINPUT_ITEM=clump, the
// default) or floppy discs (disc). CLIENT: at 26, 27 and 28 s takes the nearest item not yet taken into its inventory
// through the player's own collectObject; at 32 s (once the walk ends) opens the bin's screen through the gamemode's
// openPropInv; 2 s later, three times VOTVCOOP_BINPUT_GAP ms apart (default 4000), selects the newest record of the
// item's class in its own pane, clears the hover and runs the screen's em_take, as the hotbar_1 key does; 14 s after it
// opened closes the screen and 2 s later opens it again. BOTH, every second from 20 s to 110 s: the bin's records and,
// on the client, its own records of the class, the screen's container and the container act lane's open act
// (`[BINPUT]`).

#include "coop/dev/binput_probe.h"

#include "container_route_drill_detail.h"

#include "coop/config/config.h"
#include "coop/dev/director/director.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_drive.h"
#include "coop/player/remote_player.h"
#include "coop/props/container_act_request.h"
#include "ue_wrap/actors/container_inventory.h"
#include "ue_wrap/actors/container_view.h"
#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::binput_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
namespace CV = ue_wrap::container_view;
namespace ci = ue_wrap::container_inventory;
namespace RD = coop::dev::container_route_drill::detail;
namespace CR = coop::props::container_act_request;

constexpr const wchar_t* kBinClass = L"prop_garbageBin_green_C";
constexpr const wchar_t* kBinPath  = L"/Game/objects/prop_garbageBin_green.prop_garbageBin_green_C";

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_BINPUT_PROBE") == "1";
    return on;
}

// The bin: one the host spawns (the default) or the save's own nearest green bin (VOTVCOOP_BINPUT_BIN=map), which
// the client is moved beside at 30 s.
bool MapBin() {
    static const bool m = coop::config::ReadEnv("VOTVCOOP_BINPUT_BIN") == "map";
    return m;
}

// The gap between the three puts (VOTVCOOP_BINPUT_GAP, ms, default 4000): a player's quick clicks are a few hundred.
uint64_t Gap() {
    static const uint64_t g = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_BINPUT_GAP");
        return v.empty() ? 4000ull : static_cast<uint64_t>(std::atoll(v.c_str()));
    }();
    return g;
}

bool Disc() {
    static const bool d = coop::config::ReadEnv("VOTVCOOP_BINPUT_ITEM") == "disc";
    return d;
}
const wchar_t* ItemClass() { return Disc() ? L"prop_floppyDisc_C" : L"prop_garbageClump_C"; }
const wchar_t* ItemPath() {
    return Disc() ? L"/Game/objects/prop_floppyDisc.prop_floppyDisc_C" : L"/Game/objects/prop_garbageClump.prop_garbageClump_C";
}

void* Class(const wchar_t* name, const wchar_t* path) {
    void* cls = OI::ClassByName(name);
    return cls ? cls : ue_wrap::asset_load::LoadObjectByPath(path);
}

void* Obj(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? *reinterpret_cast<void* const*>(static_cast<const char*>(obj) + off) : nullptr;
}

std::vector<void*> All(const wchar_t* name) {
    std::vector<void*> out;
    void* const cls = OI::ClassByName(name);
    if (cls)
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

// The bin the host placed: the green bin nearest the local player.
void* Bin() {
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector at{};
    if (!me || !E::TryGetActorLocation(me, at)) return nullptr;
    void* best = nullptr;
    float bestD = 1e30f;
    for (void* b : All(kBinClass)) {
        ue_wrap::FVector p{};
        if (!E::TryGetActorLocation(b, p)) continue;
        const float d = (p.X - at.X) * (p.X - at.X) + (p.Y - at.Y) * (p.Y - at.Y) + (p.Z - at.Z) * (p.Z - at.Z);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

int BinRecords(void* bin) {
    std::vector<ue_wrap::save_record::SaveRecord> recs;
    int32_t count = -1;
    void* const inv = bin ? ci::InventoryOf(bin) : nullptr;
    if (!inv) return -1;
    ci::ReadContents(inv, recs, 64, &count);
    return count;
}

int OwnOfClass(int32_t* newest = nullptr) {
    ue_wrap::inventory::LivePersonalStore store;
    if (!ue_wrap::inventory::ReadLivePersonalStore(store)) return -1;
    int n = 0;
    for (size_t i = 0; i < store.records.size(); ++i)
        if (store.records[i].className == ItemClass()) {
            ++n;
            if (newest) *newest = static_cast<int32_t>(i);
        }
    return n;
}

void Report(bool host) {
    void* const bin = Bin();
    if (host) {
        UE_LOGI("[BINPUT] bin %hs records=%d", bin ? "live" : "none", BinRecords(bin));
        return;
    }
    UE_LOGI("[BINPUT] bin %hs records=%d own=%d screen=%hs pendingAct=%u lastRefusal=%u", bin ? "live" : "none",
            BinRecords(bin), OwnOfClass(), CV::Viewed() == nullptr ? "closed" : CV::Viewed() == bin ? "the bin" : "other",
            static_cast<unsigned>(CR::dev::PendingId()), static_cast<unsigned>(CR::dev::LastRefusal()));
}

// The hotbar key's put: the own pane's newest record of the class selected, the hover cleared, the screen's em_take.
void Put(int n) {
    void* const ui = CV::InventoryScreen();
    int32_t sel = -1;
    OwnOfClass(&sel);
    const int32_t off = ui ? R::FindPropertyOffset(R::ClassOf(ui), L"selected") : -1;
    if (!ui || sel < 0 || off < 0 || !RD::ClearHover(ui)) {
        UE_LOGW("[BINPUT] client: put %d -- nothing to select (screen %p, record %d)", n, ui, sel);
        return;
    }
    std::memcpy(static_cast<uint8_t*>(ui) + off, &sel, sizeof(sel));
    RD::CallNoArg(ui, L"updSelect");
    CV::PutState st{};
    const char* failed = "";
    const bool read = CV::ReadPutState(ui, st, &failed);
    const int before = BinRecords(Bin()), ownBefore = OwnOfClass();
    const bool ran = CV::RunPut(ui);
    UE_LOGI("[BINPUT] client: put %d -- record %d selected (read=%d takeEnabled=%d hovered=%d index=%d %hs); em_take ran=%d, "
            "bin %d -> %d, own %d -> %d, pendingAct=%u",
            n, sel, read ? 1 : 0, st.takeEnabled ? 1 : 0, st.hovered ? 1 : 0, st.index, failed, ran ? 1 : 0, before,
            BinRecords(Bin()), ownBefore, OwnOfClass(), static_cast<unsigned>(CR::dev::PendingId()));
}

uint64_t g_startAt = 0, g_reportAt = 0;
int g_step = 0;
std::vector<void*> g_taken;
bool g_moved = false;
uint64_t g_openAt = 0;
std::shared_ptr<coop::director::BackgroundWalk> g_walk;
constexpr float kWalkReachCm = 110.f;   // the walk ends this near the bin, within the host's reach

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[BINPUT] probe armed role=%s item=%ls", host ? "host" : "client", ItemClass());
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    if (host && g_step == 0 && t >= 20000u) {
        coop::RemotePlayer& rp = coop::puppet_drive::Puppet(1);
        void* const puppet = rp.valid() ? rp.GetActor() : nullptr;
        ue_wrap::FVector at{};
        if (puppet && E::TryGetActorLocation(puppet, at)) {   // a late joiner's puppet is waited for
            g_step = 1;
            const ue_wrap::FVector f = E::GetActorForwardVector(puppet);
            void* const bin = MapBin() ? nullptr
                                       : E::SpawnActor(Class(kBinClass, kBinPath), {at.X + f.X * 120.f, at.Y + f.Y * 120.f, at.Z - 40.f});
            int items = 0;
            for (int i = 0; i < 3; ++i)
                items += E::SpawnActor(Class(ItemClass(), ItemPath()),
                                       {at.X - f.Y * (50.f + 25.f * i), at.Y + f.X * (50.f + 25.f * i), at.Z - 20.f}) ? 1 : 0;
            UE_LOGI("[BINPUT] host: spawned the bin (ok=%d) and %d of 3 items", bin ? 1 : 0, items);
        }
    }
    // The three into the inventory, the nearest not yet taken each second, as item_trip_probe takes one.
    if (!host && g_step < 3 && t >= 26000u + 1000u * g_step) {
        void* best = nullptr;
        float bestD = 1e30f;
        ue_wrap::FVector m{};
        if (me && E::TryGetActorLocation(me, m))
            for (void* it : All(ItemClass())) {
                bool taken = false;
                for (void* k : g_taken) taken = taken || k == it;
                ue_wrap::FVector p{};
                if (taken || !E::TryGetActorLocation(it, p)) continue;
                const float d = (p.X - m.X) * (p.X - m.X) + (p.Y - m.Y) * (p.Y - m.Y) + (p.Z - m.Z) * (p.Z - m.Z);
                if (d < bestD) { bestD = d; best = it; }
            }
        ++g_step;
        if (best) {
            g_taken.push_back(best);
            const int32_t laOff = R::FindPropertyOffset(R::ClassOf(me), L"lookAtActor");
            const int32_t lcOff = R::FindPropertyOffset(R::ClassOf(me), L"lookAtComponent");
            if (laOff >= 0) *reinterpret_cast<void**>(static_cast<char*>(me) + laOff) = best;
            if (lcOff >= 0) *reinterpret_cast<void**>(static_cast<char*>(me) + lcOff) = Obj(best, L"StaticMesh");
            const bool ran = R::CallFunction(me, R::FindDispatchFunctionCached(R::ClassOf(me), L"collectObject"), nullptr);
            UE_LOGI("[BINPUT] client: collectObject on item %d ran=%d, own records of the class now %d", g_step, ran ? 1 : 0,
                    OwnOfClass());
        } else {
            UE_LOGW("[BINPUT] client: no item left to take for %d", g_step);
        }
    }
    // The save's bin is walked to, as a player does, so the host's puppet stands where the client does.
    if (!host && g_step == 3 && MapBin() && t >= 29000u && !g_moved) {
        g_moved = true;
        void* const bin = Bin();
        ue_wrap::FVector b{};
        if (bin && E::TryGetActorLocation(bin, b)) g_walk = coop::director::StartBackgroundWalk(b, kWalkReachCm, 40);
        UE_LOGI("[BINPUT] client: walks to the save's bin at (%.0f, %.0f, %.0f) (its key %ls)", b.X, b.Y, b.Z,
                bin ? ue_wrap::prop::GetInteractableKeyString(bin).c_str() : L"-");
    }
    const bool walked = !MapBin() || (g_walk && g_walk->state.load() != 0);
    if (!host && g_step == 3 && t >= 32000u && walked) {
        g_step = 4;
        g_openAt = t;
        void* const bin = Bin();
        UE_LOGI("[BINPUT] client: opened the bin's screen (walk state %d, ok=%d)", g_walk ? g_walk->state.load() : -1,
                bin && CV::Open(bin) ? 1 : 0);
    }
    if (!host && g_step >= 4 && g_step <= 6 && t >= g_openAt + 2000u + Gap() * (g_step - 4)) {
        Put(g_step - 3);
        ++g_step;
    }
    if (!host && g_step == 7 && t >= g_openAt + 14000u) {
        g_step = 8;
        UE_LOGI("[BINPUT] client: closed the screen (ok=%d)", CV::Close() ? 1 : 0);
    }
    if (!host && g_step == 8 && t >= g_openAt + 16000u) {
        g_step = 9;
        void* const bin = Bin();
        UE_LOGI("[BINPUT] client: opened the bin's screen again (ok=%d)", bin && CV::Open(bin) ? 1 : 0);
    }
    if (t < 20000u || t > 110000u || Now() < g_reportAt) return;
    g_reportAt = Now() + 1000;
    Report(host);
}

}  // namespace coop::dev::binput_probe
