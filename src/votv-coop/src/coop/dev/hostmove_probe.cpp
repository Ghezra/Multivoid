// coop/dev/hostmove_probe.cpp -- [dev] a client spawns a prop and the host moves it (VOTVCOOP_HOSTMOVE_PROBE=1 on
// every peer; VOTVCOOP_HOSTMOVE_ROW names the catalog row, default crowbar; VOTVCOOP_HOSTMOVE_PUT=1 has the host put
// it into its inventory and back out instead of carrying it).
//
// A client's clock starts once its world is announced and its join is over, the host's at its connect. CLIENT SLOT 1:
// at 24 s looks 35 degrees down; at 25 s spawns the row through the spawn menu's own spawn(FName), the birth a player's
// click makes (coop/dev/menu_spawn_drive), and logs the new actor's key. HOST: takes the class's actors at 5 s; the
// first actor of the class it did not hold then is the client's prop. 10 s after it appeared (not before 35 s) the host
// walks beside it, grabs it with the director's aimed grab, lifts the view and carries it 250 cm away from the client,
// then lets go (dropGrabObject); with PUT it walks beside it, takes it through collectObject and 4 s later drops it out
// of the inventory through playerContainer.getObject. EVERY PEER, every second from 20 s (80 s on a client; on the host
// 45 s after the move ended, 200 s at most): every actor of the class, its key, eid, mirror flag, place and bHidden; the
// key the prop was born under, whether an actor holds it and where; on the spawner whether its own actor still lives and
// is the key's holder (`[HOSTMOVE]`).

#include "coop/dev/hostmove_probe.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/dev/director/director.h"
#include "coop/dev/menu_spawn_drive.h"
#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/prop_key_index.h"
#include "coop/session/join_progress.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::hostmove_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
namespace DR = coop::director;
namespace MS = coop::dev::menu_spawn_drive;
using ue_wrap::FVector;

constexpr float    kStandGapCm   = 70.f;    // the host stands this far past the prop's face, on the client's side
constexpr float    kStandReachCm = 35.f;
constexpr float    kCarryCm      = 250.f;
constexpr float    kCarryReachCm = 40.f;
constexpr float    kLiftPitchDeg = 15.f;
constexpr int      kWalkDeadlineS = 25;
constexpr uint64_t kStepBoundMs  = 30000;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_HOSTMOVE_PROBE") == "1";
    return v;
}
bool Put() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_HOSTMOVE_PUT") == "1";
    return v;
}
const std::wstring& Row() {
    static const std::wstring r = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_HOSTMOVE_ROW");
        return v.empty() ? std::wstring(L"crowbar") : std::wstring(v.begin(), v.end());
    }();
    return r;
}

float Dist(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
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

ue_wrap::CachedObjRef g_cls;   // the row's class, once this world names it

std::vector<void*> Actors() {
    std::vector<void*> out;
    if (!g_cls.Alive()) g_cls.Set(MS::RowClass(Row()));
    if (void* const cls = g_cls.Get())
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

// eid and mirror flag of `a` through the element registry's reverse: (-1, -1) when it is no element.
void EidOf(void* a, long long& eid, int& mirror) {
    const coop::element::ElementId id = coop::element::Registry::Get().EidForActor(a);
    eid = id == coop::element::kInvalidId ? -1 : static_cast<long long>(id);
    const coop::element::Element* el = id == coop::element::kInvalidId ? nullptr : coop::element::Registry::Get().Get(id);
    mirror = el ? (el->IsMirror() ? 1 : 0) : -1;
}

uint64_t g_startAt = 0, g_reportAt = 0;
bool g_armedLogged = false;
int g_clientStep = 0;              // the spawner's: 0 waiting, 1 looked down, 2 spawned
std::wstring g_key;                // the key the client's prop was born under, as this peer learned it
ue_wrap::CachedObjRef g_mine;      // the spawner's own actor
std::vector<void*> g_baseline;     // the host's actors of the class at 5 s
bool g_baselined = false;
ue_wrap::CachedObjRef g_target;    // the host's actor of the client's prop
uint64_t g_foundAt = 0, g_moveEndAt = 0;

enum class Move : uint8_t { Idle, Walk, Grab, Lift, Carry, Collect, TakeOut, Done };
Move g_move = Move::Idle;
uint64_t g_stepAt = 0;
std::shared_ptr<DR::BackgroundWalk> g_walk;
std::unique_ptr<DR::AimedGrab> g_grab;
FVector g_away{1.f, 0.f, 0.f}, g_rest{};
int g_liftTicks = 0;

void To(Move m) {
    g_move = m;
    g_stepAt = Now();
}
void EndMove(const char* why) {
    if (g_walk) DR::CancelBackgroundWalk(*g_walk);
    g_walk.reset();
    g_grab.reset();
    UE_LOGI("[HOSTMOVE] host: move ended -- %s", why);
    g_move = Move::Done;
    g_moveEndAt = Now();
}

// The inventory's drop of the record of `key` (item_trip_probe's): playerContainer.getObject at the record's index.
void* TakeOut(const std::wstring& key, int32_t& index) {
    index = -1;
    ue_wrap::inventory::LivePersonalStore store;
    if (!ue_wrap::inventory::ReadLivePersonalStore(store)) return nullptr;
    for (size_t i = 0; i < store.records.size(); ++i)
        if (store.records[i].key == key) index = static_cast<int32_t>(i);
    void* const container = Field(ue_wrap::world_singleton::Gamemode(), L"playerContainer");
    if (index < 0 || !container || !R::IsLive(container)) return nullptr;
    ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(container), L"getObject"));
    const FVector none{};
    if (!f.valid() || !f.Set<int32_t>(L"index", index) || !f.Set(L"customLoc", false) ||
        !f.SetRaw(L"loc", &none, sizeof(none)) || !ue_wrap::Call(container, f))
        return nullptr;
    void* const out = f.Get<void*>(L"OutputPin");
    return out && R::IsLive(out) ? out : nullptr;
}

size_t StoreHolds(const std::wstring& key) {
    ue_wrap::inventory::LivePersonalStore store;
    size_t n = 0;
    if (ue_wrap::inventory::ReadLivePersonalStore(store))
        for (const auto& r : store.records) n += r.key == key ? 1 : 0;
    return n;
}

void HostMoveTick() {
    if (g_move == Move::Idle || g_move == Move::Done) return;
    void* const me = coop::players::Registry::Get().Local();
    void* const prop = g_target.Get();
    if (!me) return;
    if (Now() - g_stepAt > kStepBoundMs) { EndMove("a step ran out of time"); return; }
    switch (g_move) {
    case Move::Walk: {
        const int st = g_walk ? g_walk->state.load() : 2;
        if (st == 0) return;
        g_walk.reset();
        if (!prop) { EndMove("the prop died during the walk to it"); return; }
        if (st != 1) { EndMove("the walk to the prop did not arrive"); return; }
        if (Put()) {
            To(Move::Collect);
            return;
        }
        g_grab = std::make_unique<DR::AimedGrab>(me, prop);
        UE_LOGI("[HOSTMOVE] host: beside the prop, aiming the grab");
        To(Move::Grab);
        return;
    }
    case Move::Grab: {
        if (!prop) { EndMove("the prop died before the grab took"); return; }
        const DR::GrabState gs = g_grab->Tick();
        if (gs == DR::GrabState::Working) return;
        if (gs == DR::GrabState::Failed) {
            UE_LOGW("[HOSTMOVE] host: the grab failed (noAim=%d): %s", g_grab->NoAim() ? 1 : 0, g_grab->Why());
            EndMove("the grab failed");
            return;
        }
        UE_LOGI("[HOSTMOVE] host: the prop is in the hand after %d aim pose(s), grabbing=%d", g_grab->AimPoses(),
                DR::Grabbing(me) == prop ? 1 : 0);
        g_grab.reset();
        E::TryGetActorLocation(prop, g_rest);
        g_liftTicks = 0;
        To(Move::Lift);
        return;
    }
    case Move::Lift: {
        if (void* ctrl = E::GetController(me)) {
            ue_wrap::FRotator view = E::GetControlRotation(ctrl);
            view.Pitch = kLiftPitchDeg;
            E::SetControlRotation(ctrl, view);
        }
        FVector at{}, m{};
        const bool off = prop && E::TryGetActorLocation(prop, at) && Dist(at, g_rest) >= 25.f;
        if (!off && ++g_liftTicks < 120) return;
        if (!E::TryGetActorLocation(me, m)) { EndMove("the host's place could not be read"); return; }
        const FVector goal{m.X + g_away.X * kCarryCm, m.Y + g_away.Y * kCarryCm, m.Z};
        g_walk = DR::StartBackgroundWalk(goal, kCarryReachCm, kWalkDeadlineS, /*carry=*/true);
        UE_LOGI("[HOSTMOVE] host: carries the prop %.0f cm to (%.0f, %.0f, %.0f), lifted=%d", kCarryCm, goal.X, goal.Y,
                goal.Z, off ? 1 : 0);
        To(Move::Carry);
        return;
    }
    case Move::Carry: {
        const int st = g_walk ? g_walk->state.load() : 2;
        if (st == 0) return;
        g_walk.reset();
        const bool held = prop && DR::Grabbing(me) == prop;
        const bool ran = DR::CallOnPlayer(me, L"dropGrabObject");
        FVector at{};
        if (prop) E::TryGetActorLocation(prop, at);
        UE_LOGI("[HOSTMOVE] host: carry walk %hs, held=%d, dropGrabObject ran=%d, the prop at (%.0f, %.0f, %.0f), %.0f cm "
                "from where it rested", st == 1 ? "arrived" : "FAILED", held ? 1 : 0, ran ? 1 : 0, at.X, at.Y, at.Z,
                Dist(at, g_rest));
        EndMove("carried and let go");
        return;
    }
    case Move::Collect: {
        if (!prop) { EndMove("the prop died before the collect"); return; }
        const int32_t laOff = R::FindPropertyOffset(R::ClassOf(me), L"lookAtActor");
        const int32_t lcOff = R::FindPropertyOffset(R::ClassOf(me), L"lookAtComponent");
        if (laOff >= 0) *reinterpret_cast<void**>(static_cast<char*>(me) + laOff) = prop;
        if (lcOff >= 0) *reinterpret_cast<void**>(static_cast<char*>(me) + lcOff) = Field(prop, L"StaticMesh");
        const bool ran = R::CallFunction(me, R::FindDispatchFunctionCached(R::ClassOf(me), L"collectObject"), nullptr);
        UE_LOGI("[HOSTMOVE] host: collectObject on the prop ran=%d, the store holds the key %zu time(s)", ran ? 1 : 0,
                StoreHolds(g_key));
        To(Move::TakeOut);
        return;
    }
    case Move::TakeOut: {
        if (Now() - g_stepAt < 4000) return;
        int32_t index = -1;
        void* const out = TakeOut(g_key, index);
        FVector at{};
        if (out) E::TryGetActorLocation(out, at);
        if (out) g_target.Set(out);
        UE_LOGI("[HOSTMOVE] host: getObject on record %d -- out=%d key=%ls at (%.0f, %.0f, %.0f), the store holds the key "
                "%zu time(s)", index, out ? 1 : 0, out ? ue_wrap::prop::GetInteractableKeyString(out).c_str() : L"-", at.X,
                at.Y, at.Z, StoreHolds(g_key));
        EndMove("put into the inventory and out");
        return;
    }
    default:
        return;
    }
}

void HostStartMove() {
    void* const me = coop::players::Registry::Get().Local();
    void* const prop = g_target.Get();
    FVector at{}, origin{}, ext{};
    if (!me || !prop || !E::TryGetActorLocation(prop, at)) { EndMove("the prop could not be read at the start"); return; }
    if (!E::GetActorBounds(prop, true, origin, ext)) ext = {20.f, 20.f, 20.f};
    // Away from client slot 1's body, in the ground plane; the host's own forward when the body cannot be read.
    FVector from{};
    coop::RemotePlayer* const rp = coop::players::Registry::Get().Puppet(1);
    if (!(rp && rp->valid() && rp->TryGetLocation(from))) from = {at.X - E::GetActorForwardVector(me).X, at.Y - E::GetActorForwardVector(me).Y, at.Z};
    float dx = at.X - from.X, dy = at.Y - from.Y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1.f) { dx /= len; dy /= len; } else { dx = 1.f; dy = 0.f; }
    g_away = {dx, dy, 0.f};
    const float gap = (std::max)(ext.X, ext.Y) + kStandGapCm;
    const FVector stand{at.X - dx * gap, at.Y - dy * gap, at.Z};
    g_walk = DR::StartBackgroundWalk(stand, kStandReachCm, kWalkDeadlineS, /*carry=*/false, prop);
    UE_LOGI("[HOSTMOVE] host: walks to (%.0f, %.0f, %.0f) beside the prop at (%.0f, %.0f, %.0f) (%hs)", stand.X, stand.Y,
            stand.Z, at.X, at.Y, at.Z, Put() ? "to put it into the inventory" : "to carry it");
    To(Move::Walk);
}

void Report(bool host, uint64_t t) {
    const std::vector<void*> all = Actors();
    void* const holder = g_key.empty() ? nullptr : coop::prop_key_index::Find(g_key);
    FVector hAt{};
    const bool hLive = holder && R::IsLive(holder) && E::TryGetActorLocation(holder, hAt);
    long long eid = -1;
    int mirror = -1;
    if (hLive) EidOf(holder, eid, mirror);
    char mine[96] = "";
    if (!host && g_clientStep == 2)
        snprintf(mine, sizeof(mine), " mineLive=%d mineIsHolder=%d", g_mine.Get() ? 1 : 0,
                 hLive && g_mine.Get() == holder ? 1 : 0);
    UE_LOGI("[HOSTMOVE] t=%llu %hs class actors=%zu key=%ls holder=%hs at (%.0f, %.0f, %.0f) eid=%lld mirror=%d bHidden=%d%hs",
            t / 1000ull, host ? "host" : "client", all.size(), g_key.empty() ? L"(unknown here)" : g_key.c_str(),
            g_key.empty() ? "-" : hLive ? "live" : "NONE", hAt.X, hAt.Y, hAt.Z, eid, mirror, hLive ? Bit(holder, L"bHidden") : -1,
            mine);
    int n = 0;
    for (void* a : all) {
        if (++n > 6) break;
        FVector p{};
        E::TryGetActorLocation(a, p);
        long long e = -1;
        int m = -1;
        EidOf(a, e, m);
        UE_LOGI("[HOSTMOVE]   actor key=%ls at (%.0f, %.0f, %.0f) eid=%lld mirror=%d bHidden=%d",
                ue_wrap::prop::GetInteractableKeyString(a).c_str(), p.X, p.Y, p.Z, e, m, Bit(a, L"bHidden"));
    }
}

bool ClientReady() {
    void* const me = coop::players::Registry::Get().Local();
    return me && R::IsLive(me) && coop::net_pump::HasAnnouncedWorldReady() &&
           coop::join_progress::CurrentPhase() == coop::join_progress::Phase::Idle;
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
        g_reportAt = 0;
        if (!g_armedLogged) {
            g_armedLogged = true;
            UE_LOGI("[HOSTMOVE] probe armed role=%s slot=%u row=%ls put=%d", host ? "host" : "client",
                    static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId()), Row().c_str(), Put() ? 1 : 0);
        }
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    const bool spawner = !host && coop::players::Registry::Get().LocalPeerId() == 1;

    if (spawner && g_clientStep == 0 && t >= 24000u && me) {
        g_clientStep = 1;
        if (void* ctrl = E::GetController(me)) {
            ue_wrap::FRotator view = E::GetControlRotation(ctrl);
            view.Pitch = -35.f;
            E::SetControlRotation(ctrl, view);
        }
    }
    if (spawner && g_clientStep == 1 && t >= 25000u) {
        g_clientStep = 2;
        const std::vector<void*> before = Actors();
        const bool inCatalog = MS::CatalogHas(Row());
        const bool ran = MS::Spawn(Row());
        void* born = nullptr;
        for (void* a : Actors())
            if (std::find(before.begin(), before.end(), a) == before.end()) born = a;
        FVector at{};
        if (born) {
            g_mine.Set(born);
            g_key = ue_wrap::prop::GetInteractableKeyString(born);
            E::TryGetActorLocation(born, at);
        }
        UE_LOGI("[HOSTMOVE] client: spawned row '%ls' through the spawn menu (inCatalog=%d menuReady=%d ran=%d born=%d) key=%ls "
                "at (%.0f, %.0f, %.0f)", Row().c_str(), inCatalog ? 1 : 0, MS::Ready() ? 1 : 0, ran ? 1 : 0, born ? 1 : 0,
                g_key.c_str(), at.X, at.Y, at.Z);
    }

    if (host && !g_baselined && t >= 5000u) {
        g_baselined = true;
        g_baseline = Actors();
        UE_LOGI("[HOSTMOVE] host: %zu actor(s) of the row's class before any client spawn", g_baseline.size());
    }
    if (host && g_baselined && !g_foundAt && t >= 20000u) {
        for (void* a : Actors())
            if (std::find(g_baseline.begin(), g_baseline.end(), a) == g_baseline.end()) {
                g_target.Set(a);
                g_key = ue_wrap::prop::GetInteractableKeyString(a);
                g_foundAt = Now();
                long long eid = -1;
                int mirror = -1;
                EidOf(a, eid, mirror);
                UE_LOGI("[HOSTMOVE] host: the client's prop appeared key=%ls eid=%lld mirror=%d", g_key.c_str(), eid, mirror);
                break;
            }
    }
    if (host && g_foundAt && g_move == Move::Idle && t >= 35000u && Now() - g_foundAt >= 10000u) HostStartMove();
    if (host) HostMoveTick();

    const uint64_t endAt = host ? (g_moveEndAt ? (std::min<uint64_t>)(g_moveEndAt - g_startAt + 45000u, 200000u) : 200000u)
                                : 80000u;
    if (t < 20000u || t > endAt || Now() < g_reportAt) return;
    g_reportAt = Now() + 1000;
    Report(host, t);
}

}  // namespace coop::dev::hostmove_probe
