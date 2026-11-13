// coop/creatures/orb_authority.cpp -- see coop/creatures/orb_authority.h.
//
// The orb's graph, from its bytecode (0.9.0n): ReceiveBeginPlay starts a wander (AIMoveTo a random reachable point,
// again 1 s after each move ends while beginevent is false), saves, and raises the event. The overlap stub casts its
// OtherActor to mainPlayer_C and calls begin: StopActiveMovement, beginevent = true, timeline "b" from its start.
// The timeline's update fades the orb's material in and swells its sounds; its end (b__FinishedFunc) takes player
// 0's camera, and dot(camera forward, normal(camera - orb)) > 0 -- the orb behind the camera -- destroys the orb;
// otherwise a burst of 2D sounds, the glow and the light, then QuitGame and lib_C::End.

#include "coop/creatures/orb_authority.h"

#include "coop/creatures/npc_sync.h"
#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/death_revive.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_drive.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/facts/function_row.h"

#include <cmath>
#include <vector>

namespace coop::orb_authority {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace sg = ue_wrap::script_gate;
namespace EL = coop::element;
namespace N  = coop::net;

UE_FACT_FN(kRowOrbBegin, "orb_authority", L"npc_orborb_C", L"begin", Declared);
UE_FACT_FN(kRowOrbFinished, "orb_authority", L"npc_orborb_C", L"b__FinishedFunc", Declared);

constexpr int kTagBegin = 0x4F524201;     // 'ORB' 1
constexpr int kTagFinished = 0x4F524202;  // 'ORB' 2

coop::net::Session* g_session = nullptr;
bool g_watched = false;
bool g_settled = false;

// HOST: an orb whose encounter began, who touched it (a slot; 0 the host), and whether its Begin was sent.
struct Encounter {
    ue_wrap::CachedObjRef orb;
    uint8_t toucher = 0;
    bool    sent = false;
};
std::vector<Encounter> g_encounters;

bool IsHost() { return g_session && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->role() == N::Role::Client; }

Encounter* Find(void* orb) {
    for (Encounter& e : g_encounters)
        if (e.orb.Is(orb)) return &e;
    return nullptr;
}

Encounter& Record(void* orb, uint8_t toucher) {
    if (Encounter* e = Find(orb)) return *e;
    g_encounters.emplace_back();
    g_encounters.back().orb.Set(orb);
    g_encounters.back().toucher = toucher;
    return g_encounters.back();
}

bool BeganAlready(void* orb) {
    int32_t off = -1;
    uint8_t mask = 0;
    return R::FindBoolProperty(R::ClassOf(orb), L"beginevent", off, mask) && off >= 0 &&
           (*(static_cast<uint8_t*>(orb) + off) & mask) != 0;
}

bool CallBegin(void* orb) {
    void* fn = R::FindFunction(R::ClassOf(orb), L"begin");
    return fn && R::CallFunction(orb, fn, nullptr);
}

void Send(void* orb, uint8_t op, int slot) {
    if (!g_session) return;
    N::OrbEventPayload p{};
    p.eid = static_cast<uint32_t>(EL::Registry::Get().EidForActor(orb));
    p.op = op;
    if (p.eid == static_cast<uint32_t>(EL::kInvalidId)) {
        UE_LOGW("orb_authority: an orb with no element id -- its %hs reaches no client",
                op == N::kOrbEventBegin ? "begin" : "death");
        return;
    }
    const bool ok = slot >= 0 ? g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::OrbEvent, &p,
                                                              sizeof(p))
                              : g_session->SendReliable(N::ReliableKind::OrbEvent, &p, sizeof(p));
    UE_LOGI("orb_authority: HOST sent orb eid %u %hs to %hs (ok=%d)", p.eid,
            op == N::kOrbEventBegin ? "begin" : "death", slot >= 0 ? "its toucher" : "every client", ok ? 1 : 0);
}

// The orb's own facing test against a viewer at `cam` looking along `fwd`: true when the orb is behind the view.
bool BehindView(const ue_wrap::FVector& cam, const ue_wrap::FVector& fwd, const ue_wrap::FVector& orb) {
    const float dx = cam.X - orb.X, dy = cam.Y - orb.Y, dz = cam.Z - orb.Z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4f) return false;
    return (fwd.X * dx + fwd.Y * dy + fwd.Z * dz) / len > 0.f;
}

ue_wrap::FVector Forward(const ue_wrap::FRotator& r) {
    constexpr float kDeg = 3.14159265f / 180.f;
    const float cp = std::cos(r.Pitch * kDeg), sp = std::sin(r.Pitch * kDeg);
    const float cy = std::cos(r.Yaw * kDeg), sy = std::sin(r.Yaw * kDeg);
    return {cp * cy, cp * sy, sp};
}

// The toucher's view: the host's camera, or a client's streamed eye and newest aim. False when it cannot be read.
bool ViewOf(uint8_t slot, ue_wrap::FVector& cam, ue_wrap::FVector& fwd) {
    if (slot == 0) {
        cam = E::GetCameraLocation();
        fwd = Forward(E::GetCameraRotation());
        return true;
    }
    coop::RemotePlayer& rp = coop::puppet_drive::Puppet(slot);
    if (!rp.valid() || !rp.TryGetEyePosition(cam)) return false;
    fwd = rp.GetLatestAimDirection();
    return true;
}

// Begin's watch. HOST: send every client the encounter's start, once per orb.
sg::Verdict OnBeginPre(const sg::Call& c) {
    if (!IsHost() || !c.object) return sg::Verdict::Run;
    Encounter& e = Record(c.object, 0);
    if (!e.sent && !BeganAlready(c.object)) {
        e.sent = true;
        Send(c.object, N::kOrbEventBegin, -1);
    }
    return sg::Verdict::Run;
}

// The timeline's end. A client's copy runs nothing; the host judges the toucher and destroys the orb.
sg::Verdict OnFinishedPre(const sg::Call& c) {
    if (IsClient()) return sg::Verdict::Cancel;
    if (!IsHost() || !c.object) return sg::Verdict::Run;
    void* const orb = c.object;
    const Encounter* e = Find(orb);
    const uint8_t slot = e ? e->toucher : 0;
    ue_wrap::FVector at{}, cam{}, fwd{};
    const bool read = E::TryGetActorLocation(orb, at) && ViewOf(slot, cam, fwd);
    // A view that cannot be read (the toucher left) spares the toucher, as nobody is there to end.
    const bool survives = !read || BehindView(cam, fwd, at);
    UE_LOGI("orb_authority: HOST orb's verdict for slot %u -- %hs%hs", static_cast<unsigned>(slot),
            survives ? "spared (the orb is behind its view)" : "caught (the orb is in its view)",
            read ? "" : ", its view did not read");
    if (!survives) {
        if (slot == 0) {
            if (coop::death_revive::ReviveAvailable())
                coop::death_revive::NoteRunEndCancelled(nullptr, L"the black orb", false);
        } else {
            Send(orb, N::kOrbEventDeath, slot);
        }
    }
    for (auto it = g_encounters.begin(); it != g_encounters.end(); ++it)
        if (it->orb.Is(orb)) { g_encounters.erase(it); break; }
    E::DestroyActor(orb);
    return sg::Verdict::Cancel;
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    const bool b = sg::WatchName(kRowOrbBegin, kTagBegin, &OnBeginPre, nullptr);
    const bool f = sg::WatchName(kRowOrbFinished, kTagFinished, &OnFinishedPre, nullptr);
    if (!b || !f) UE_LOGE("orb_authority: a watch was refused (begin=%d finished=%d)", b ? 1 : 0, f ? 1 : 0);
}

void Tick() {
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowOrbBegin, kTagBegin) || !sg::NameWatchSettled(kRowOrbFinished, kTagFinished)) return;
    g_settled = true;
    const bool b = sg::NameWatchLive(kRowOrbBegin, kTagBegin), f = sg::NameWatchLive(kRowOrbFinished, kTagFinished);
    if (b && f) UE_LOGI("orb_authority: the watches on npc_orborb_C begin and b__FinishedFunc are live");
    else UE_LOGE("orb_authority: a watch is dead (begin=%d finished=%d) -- the orb is judged by each machine", b ? 1 : 0,
                 f ? 1 : 0);
}

void OnDisconnect() { g_encounters.clear(); }

sg::Verdict OnTouchPre(void* orb, void* toucher) {
    if (!orb || !toucher) return sg::Verdict::Run;
    if (IsClient()) return sg::Verdict::Cancel;  // the host judges every touch
    if (!IsHost()) return sg::Verdict::Run;      // single player
    auto& reg = coop::players::Registry::Get();
    if (!reg.IsPuppetBody(toucher)) {
        if (toucher == reg.Local() && !BeganAlready(orb)) Record(orb, 0);
        return sg::Verdict::Run;  // the host's own player, as the game runs it
    }
    const uint8_t slot = reg.PeerIdOfBody(toucher);
    if (slot == 0 || slot >= coop::players::kMaxPeers || BeganAlready(orb) || Find(orb)) return sg::Verdict::Cancel;
    Record(orb, slot);
    UE_LOGI("orb_authority: HOST slot %u's body touched an orb -- its encounter begins on the host",
            static_cast<unsigned>(slot));
    ue_wrap::CachedObjRef ref;
    ref.Set(orb);
    ue_wrap::game_thread::Post([ref] {
        if (void* o = ref.Get()) CallBegin(o);
    });
    return sg::Verdict::Cancel;
}

void OnEvent(const coop::net::OrbEventPayload& p) {
    if (!IsClient()) return;
    if (p.op == N::kOrbEventDeath) {
        UE_LOGI("orb_authority: CLIENT the host's orb eid %u caught this player -- the run's end is answered as a death",
                p.eid);
        if (coop::death_revive::ReviveAvailable())
            coop::death_revive::NoteRunEndCancelled(nullptr, L"the black orb", false);
        return;
    }
    if (p.op != N::kOrbEventBegin) return;
    void* orb = EL::LiveActorOfType(static_cast<EL::ElementId>(p.eid), EL::ElementType::Npc);
    const bool ran = orb && CallBegin(orb);
    UE_LOGI("orb_authority: CLIENT orb eid %u's encounter began on the host -- this copy's begin ran=%d", p.eid,
            ran ? 1 : 0);
}

}  // namespace coop::orb_authority
