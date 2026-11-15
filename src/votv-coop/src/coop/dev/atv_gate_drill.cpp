// coop/dev/atv_gate_drill.cpp -- the sequencer of the ATV drill; see coop/dev/atv_gate_drill.h. The acts are
// atv_gate_drill_acts.cpp and the steps atv_gate_drill_steps.cpp.

#include "coop/dev/atv_gate_drill.h"

#include "atv_gate_drill_detail.h"  // co-located private header (src tree, not include/)
#include "atv_spawn_near.h"         // SpawnAtNav, KeyOf (co-located private header)

#include "harness/autotest.h"   // IsClientRole
#include "harness/autotest/peer_signal.h"

#include "coop/interactables/atv_drill_events.h"
#include "coop/interactables/atv_host_sync.h"
#include "coop/interactables/atv_sync.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/session/net_pump.h"
#include "coop/session/rig_verdict.h"
#include "coop/config/config.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/trace.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_nav.h"

#include "ue_wrap/core/log.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace coop::dev::atv_gate_drill {

namespace PS = harness::autotest::peer_signal;
namespace RV = coop::rig_verdict;

namespace detail {
namespace {

std::atomic<bool> g_aborted{false};
std::atomic<bool> g_said{false};
Role g_me = kHost;

const char kEndMark[] = "atv_end";

// The verdict of an end text ("F ..." or "I ..."), said once, by the host.
void SayEnd(const std::string& text) {
    if (g_me != kHost || g_said.exchange(true)) return;
    const bool fail = !text.empty() && text[0] == 'F';
    const std::string why = text.size() > 2 ? text.substr(2) : std::string();
    RV::Say(kTag, fail ? RV::Outcome::Fail : RV::Outcome::Invalid, "%s", why.c_str());
}

}  // namespace

void SetRole(Role r) { g_me = r; }

// HOST: whether any request was answered with its slot's world not ready (the park's event, phase 2). Read on every wait of the
// run, not only at step 16: with the park off, the first such answer is a joiner's own adoption sync, which comes at its
// first join and leaves its gate shut, so the run would otherwise end INVALID at the next barrier (f7-atv_park_off).
bool AnsweredUnparked() {
    static uint32_t cursor = 0;
    DE::Rec r;
    while (DE::Next(DE::kPark, cursor, r))
        if (r.i1 == 2) return true;
    return false;
}

bool Aborted() {
    if (g_aborted.load()) return true;
    if (g_me == kHost && AnsweredUnparked()) {
        UE_LOGI("atv-drill: step 16 FAIL: the host answered a request from a slot whose world was not ready (it was not parked)");
        End('F', "step 16 (a request at the joiner's world-ready edge is parked) on the host: answered before the slot's world was ready");
        return true;
    }
    std::string text;
    if (!PS::Peek(kEndMark, text)) return false;
    g_aborted.store(true);
    SayEnd(text);
    return true;
}

void End(char kind, const std::string& text) {
    const std::string line = std::string(1, kind) + " " + text;
    UE_LOGI("atv-drill: the run ends %s: %s", kind == 'F' ? "FAIL" : "INVALID", text.c_str());
    if (!g_aborted.exchange(true)) PS::Mark(kEndMark, line);
    SayEnd(line);
}

bool AwaitMark(const std::string& name, std::string& out, int boundS) {
    return WaitUntil(("the signal '" + name + "'").c_str(), boundS, [&] { return PS::Peek(name.c_str(), out); });
}

void SleepStep() { PS::detail::SleepStep(); }

void Fact(Ctx& c, const std::string& text) { PS::Mark((c.id + "_f_" + RoleName(c.me)).c_str(), text); }

bool AwaitFact(Ctx& c, Role from, std::string& text) {
    return AwaitMark(c.id + "_f_" + RoleName(from), text, kEventS);
}

}  // namespace detail

namespace {

using namespace detail;

constexpr int kJoinS = 480;     // the clients' boot, join and world load
constexpr int kSignalS = 240;   // a step of another peer
constexpr int kWalkS = 420;     // another peer's director walk
constexpr int kBarrierS = 30;   // this copy's apply of the host's state

std::wstring g_key;

std::string Mk(const std::string& id, const char* what, int role = -1) {
    return id + "_" + what + (role >= 0 ? std::string("_") + RoleName(static_cast<Role>(role)) : std::string());
}

char OutCode(Out o) { return o == Out::Pass ? 'P' : o == Out::Fail ? 'F' : o == Out::Invalid ? 'I' : 'S'; }

// One step as this peer sees it. False ends this peer's sequence (the run was decided, or this peer has nothing more to do).
bool RunStep(const Step& s) {
    Ctx c;
    c.me = g_me;
    c.key = g_key;
    c.id = s.id;
    const bool host = c.me == kHost;
    std::string why;
    // The barrier: the host sets the step's state (its prelude) and says its state sequence; each peer waits to have applied
    // it and reads its own vitals, so nothing the step decides on is the state before it.
    if (host) {
        if (s.prelude && !s.prelude(c, why)) {
            End('I', std::string("step ") + s.id + ": " + why);
            return false;
        }
        OnGt([&] { c.seq0 = coop::atv_host_sync::CurrentStateSeq(c.key); });
        PS::Mark(Mk(c.id, "go").c_str(), std::to_string(c.seq0));
    } else {
        std::string t;
        if (!AwaitMark(Mk(c.id, "go"), t, kSignalS)) return false;
        c.seq0 = static_cast<uint32_t>(std::stoul(t));
        const bool applied = WaitUntil("this copy applies the host's state", kBarrierS, [&] {
            uint32_t a = 0;
            OnGt([&] { a = coop::atv_host_sync::AppliedStateSeq(c.key); });
            return static_cast<int32_t>(a - c.seq0) >= 0;
        });
        if (!applied) return false;
    }
    const Vitals v = ReadVitals(c.key);
    c.fuel0 = v.fuel;
    c.health0 = v.health;
    c.modules0 = v.modules;
    for (int e = 0; e < DE::kEventCount; ++e) c.cur[e] = DE::Count(static_cast<DE::Event>(e));
    // Setup, then the actor's go.
    const bool setUp = !s.setup || s.setup(c, why);
    if (!host) PS::Mark(Mk(c.id, "rdy", c.me).c_str(), setUp ? "ok" : "fail: " + why);
    else {
        if (!setUp) {
            End('I', std::string("step ") + s.id + ": " + why);
            return false;
        }
        for (int r = kClient; r < kRoles; ++r) {
            std::string t;
            if (!AwaitMark(Mk(c.id, "rdy", r), t, kSignalS)) return false;
            if (t != "ok") {
                End('I', std::string("step ") + s.id + " on the " + RoleName(static_cast<Role>(r)) + ": " + t);
                return false;
            }
        }
        PS::Mark(Mk(c.id, "act").c_str());
    }
    std::string t;
    if (!host && !AwaitMark(Mk(c.id, "act"), t, kSignalS)) return false;
    if (c.me == s.actor) {
        const bool acted = s.act && s.act(c, why);
        PS::Mark(Mk(c.id, "done").c_str(), acted ? "ok" : "fail: " + why);
        if (!acted) {
            End('I', std::string("step ") + s.id + ": the act could not be issued: " + why);
            return false;
        }
    } else {
        if (!AwaitMark(Mk(c.id, "done"), t, kSignalS)) return false;
        if (t != "ok") return false;   // the actor said INVALID
    }
    // The decision, on this peer's events: a FAIL or an INVALID ends the run here.
    const Result r = s.decide ? s.decide(c) : Pass("nothing to decide here");
    UE_LOGI("atv-drill: step %s %s: %s (%s)", s.id, r.out == Out::Pass ? "PASS" : r.out == Out::Fail ? "FAIL" : r.out == Out::Invalid ? "INVALID" : "stopped", r.text.c_str(), RoleName(c.me));
    if (s.teardown) s.teardown(c);
    if (r.out == Out::Fail || r.out == Out::Invalid) {
        End(OutCode(r.out), std::string("step ") + s.id + " (" + s.what + ") on the " + RoleName(c.me) + ": " + r.text);
        return false;
    }
    if (r.out == Out::Stopped) return false;
    PS::Mark(Mk(c.id, "res", c.me).c_str(), "ok");
    // Every peer decided: the host says the step, the others wait for it.
    if (host) {
        for (int p = kClient; p < kRoles; ++p) {
            std::string res;
            if (!AwaitMark(Mk(c.id, "res", p), res, kEventS + kSignalS)) return false;
        }
        UE_LOGI("atv-drill: step %s PASS: %s", s.id, s.what);
        PS::Mark(Mk(c.id, "verdict").c_str(), "pass");
    } else {
        if (!AwaitMark(Mk(c.id, "verdict"), t, kEventS + kSignalS)) return false;
    }
    return true;
}

// The announced world and the host's puppet exist here.
bool WorldUp() {
    return WaitUntil("the announced world", kJoinS, [] {
               bool ready = false;
               OnGt([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
               return ready;
           }) &&
           WaitUntil("the host's puppet", 60, [] {
               bool have = false;
               OnGt([&] {
                   coop::RemotePlayer* p = coop::players::Registry::Get().Puppet(0);
                   have = p && p->valid();
               });
               return have;
           });
}

// VOTVCOOP_ATV_DRILL_SPAWN=1: the host spawns the drill's ATV on the NavMesh in clear sight of itself and names it,
// rather than the world's nearest, for a world whose ATVs stand where no walk arrives (one left on a roof by an earlier
// run). Empty, with `why`, when the spawn, its key or its rest failed.
// Room for an ATV at `pt`: no static, dynamic or physics body within 1.4 m of it, by eight traces out from it at the
// wheels', the seat's and the handlebars' heights (`floorZ` the host's feet). An ATV born into a bunk bed is pushed out
// with impulses that take all its health, and explodes before the drill begins (Ghezra/Multivoid#35). `what` names the
// first thing in the way.
bool RoomForAtv(void* me, const ue_wrap::FVector& pt, float floorZ, std::wstring& what) {
    static const uint8_t kTypes[] = {0, 1, 3};   // WorldStatic, WorldDynamic, PhysicsBody
    for (const float up : {40.f, 80.f, 130.f}) {
        const ue_wrap::FVector c{pt.X, pt.Y, floorZ + up};
        for (int b = 0; b < 8; ++b) {
            const float a = static_cast<float>(b) * 0.7853982f;
            const ue_wrap::FVector e{c.X + 140.f * std::cos(a), c.Y + 140.f * std::sin(a), c.Z};
            ue_wrap::trace::Hit hit;
            if (!ue_wrap::trace::LineHitForObjects(me, c, e, kTypes, 3, nullptr, 0, &hit)) {
                what = L"(no trace)";
                return false;
            }
            if (hit.blocked) {
                what = hit.actor ? ue_wrap::reflection::ClassNameOf(hit.actor) : std::wstring(L"(no actor)");
                return false;
            }
        }
    }
    return true;
}

std::wstring SpawnOwnAtv(std::string& why) {
    namespace SN = coop::dev::atv_spawn_near;
    namespace E = ue_wrap::engine;
    ue_wrap::CachedObjRef atv;
    bool spawned = false;
    OnGt([&] {
        void* const me = coop::players::Registry::Get().Local();
        ue_wrap::FVector at{};
        if (!me || !E::TryGetActorLocation(me, at)) return;
        // The first spot, by bearing round the host at 5 and 7 m that the host sees with nothing between (an act aims at
        // the ATV from beside it, and a door or a wall in the way takes the player's trace), then at 10 to 25 m with room
        // round it alone: from a cramped room like the base's dorm the director walks the players out to it.
        const ue_wrap::FVector f = E::GetActorForwardVector(me);
        // The host's capsule stands its half height above its feet.
        const float floorZ = at.Z - 85.f;
        why = "no spot round the host at 5 to 25 m is clear of things for an ATV and on the NavMesh";
        for (const float ahead : {500.f, 700.f, 1000.f, 1500.f, 2000.f, 2500.f}) {
            for (int b = 0; b < 16 && !spawned; ++b) {
                const float a = static_cast<float>(b) * 0.3926991f;
                const float dx = f.X * std::cos(a) - f.Y * std::sin(a), dy = f.X * std::sin(a) + f.Y * std::cos(a);
                const ue_wrap::FVector pt{at.X + dx * ahead, at.Y + dy * ahead, at.Z};
                ue_wrap::trace::Hit hit;
                if (ahead < 800.f && (!ue_wrap::trace::LineHitStatDyn(me, at, pt, &hit) || hit.blocked)) continue;
                // The NavMesh point the spawn will use, on the host's own floor: the query's box reaches 3 m up, onto
                // a shelf or a floor above, and the room is judged where the ATV will be born.
                ue_wrap::FVector nav{};
                if (!E::ProjectToNav(me, pt, {SN::kProjectCm, SN::kProjectCm, SN::kProjectCm}, &nav)) continue;
                if (std::fabs(nav.Z - floorZ) > 60.f) {
                    UE_LOGI("atv-drill: no room for the ATV %.0f m out at bearing %d: its NavMesh point is %.0f cm off "
                            "the host's floor", ahead / 100.f, b, nav.Z - floorZ);
                    continue;
                }
                std::wstring what;
                if (!RoomForAtv(me, nav, nav.Z, what)) {
                    UE_LOGI("atv-drill: no room for the ATV %.0f m out at bearing %d: %ls", ahead / 100.f, b, what.c_str());
                    continue;
                }
                SN::Spawned sp;
                if (!SN::SpawnAtNav(me, nav, sp)) continue;
                atv.Set(sp.actor);
                spawned = true;
            }
            if (spawned) break;
        }
    });
    if (!spawned) return {};
    std::wstring key;
    const char* bad = nullptr;
    SN::Key state = SN::Key::Wait;
    const bool keyed = WaitUntil("the spawned ATV in the host's table", 30, [&] {
        OnGt([&] {
            void* const a = atv.Get();
            state = a ? SN::KeyOf(a, &key, &bad) : SN::Key::Bad;
        });
        return state != SN::Key::Wait;
    });
    if (!keyed || state == SN::Key::Bad) {
        why = bad ? bad : "the spawned ATV did not enter the host's table";
        return {};
    }
    // Born above the NavMesh, it drops and settles: named only once its place has held still for a second, so no
    // client's copy is still being corrected onto it while an act aims at it.
    ue_wrap::FVector last{};
    int still = 0;
    const bool rested = WaitUntil("the spawned ATV at rest", 20, [&] {
        ue_wrap::FVector now{};
        bool read = false;
        OnGt([&] { void* const a = atv.Get(); read = a && E::TryGetActorLocation(a, now); });
        if (!read) return false;
        const float dx = now.X - last.X, dy = now.Y - last.Y, dz = now.Z - last.Z;
        still = dx * dx + dy * dy + dz * dz < 1.f ? still + 1 : 0;
        last = now;
        return still >= 10;   // ten reads 100 ms apart
    });
    if (!rested) {
        why = "the spawned ATV did not come to rest";
        return {};
    }
    // Whole after settling: an ATV that took a hit on the way down is no fixture for the steps.
    float health = -1.f;
    OnGt([&] { void* const a = atv.Get(); health = a ? ue_wrap::atv::GetHealth(a) : -1.f; });
    if (health < 99.5f) {
        why = "the spawned ATV was damaged settling (health " + std::to_string(health) + ")";
        return {};
    }
    UE_LOGI("atv-drill: the host spawned its own ATV for the drill, key='%ls', at rest", key.c_str());
    return key;
}

// Every peer's way into its place: the ATV chosen by the host, its copy resolved on each client, the director walking the
// client and then the host beside it.
bool Arrive() {
    std::string t;
    if (g_me == kHost) {
        for (const char* m : {"client_ready", "observer_ready"})
            if (!AwaitMark(m, t, kJoinS)) return false;
        if (coop::config::ReadEnv("VOTVCOOP_ATV_DRILL_SPAWN") == "1") {
            std::string why;
            g_key = SpawnOwnAtv(why);
            if (g_key.empty()) {
                End('I', "the host's own ATV: " + why);
                return false;
            }
        } else {
            g_key = ChooseAtv();
        }
        if (g_key.empty()) {
            End('I', "the world holds no ATV a route from the host reaches");
            return false;
        }
        UE_LOGI("atv-drill: the host's ATV key='%ls'", g_key.c_str());
        PS::Mark("atv_key", std::string(g_key.begin(), g_key.end()));
        for (const char* m : {"client_has_atv", "observer_has_atv"})
            if (!AwaitMark(m, t, kSignalS)) return false;
        if (!AwaitMark("client_at_atv", t, kWalkS)) return false;
        std::string why;
        if (!WalkToAtv(g_key, why)) {
            End('I', why);
            return false;
        }
        PS::Mark("host_at_atv");
        return true;
    }
    if (!WorldUp()) return false;
    PS::Mark(g_me == kClient ? "client_ready" : "observer_ready");
    if (!AwaitMark("atv_key", t, kSignalS)) return false;
    g_key.assign(t.begin(), t.end());
    if (!WaitUntil("the ATV copy under the host's key", 120, [] { return CopyResolves(g_key); })) return false;
    PS::Mark(g_me == kClient ? "client_has_atv" : "observer_has_atv");
    if (g_me == kClient) OnGt([] { coop::atv_sync::ArmDrillPark(); });   // step 10's ATV is the next spawn parked
    if (g_me == kClient) {
        std::string why;
        if (!WalkToAtv(g_key, why)) {
            End('I', why);
            return false;
        }
        PS::Mark("client_at_atv");
    }
    return AwaitMark("host_at_atv", t, kWalkS);
}

void Run() {
    if (!Arrive()) return;
    for (int i = 0; i < kStepCount; ++i)
        if (!RunStep(kSteps[i])) return;
    if (g_me == kClient) LeaveForRejoin();
    if (g_me == kHost && HostRejoin(g_key)) RV::Say(kTag, RV::Outcome::Pass, "every step decided on its events");
}

}  // namespace

DWORD WINAPI Thread(LPVOID /*arg*/) {
    // The drill is the host's, slot 1's (the client) and slot 2's (the observer). A client learns its slot at its join.
    Role role = detail::kHost;
    if (harness::autotest::IsClientRole()) {
        uint8_t slot = coop::players::kPeerIdUnknown;
        for (int i = 0; i < 2400; ++i) {
            slot = coop::players::Registry::Get().LocalPeerId();
            if (slot != coop::players::kPeerIdUnknown) break;
            ::Sleep(100);
        }
        if (slot != 1 && slot != 2) {
            UE_LOGI("atv-drill: this client is slot %d: it takes no part", static_cast<int>(slot));
            return 0;
        }
        role = slot == 1 ? detail::kClient : detail::kObserver;
    }
    if (!PS::Begin(detail::kTag)) return 0;
    detail::SetRole(role);
    if (role == detail::kClient && detail::IsRejoinedLife()) detail::RunRejoinedClient();
    else Run();
    return 0;
}

}  // namespace coop::dev::atv_gate_drill
