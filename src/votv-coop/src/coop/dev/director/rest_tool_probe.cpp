// coop/dev/director/rest_tool_probe.cpp -- the two-peer measure of where a dropped prop rests (coop/props/prop_settle)
// and of a toolgun's flags (coop/props/prop_tool_flags). DEV-ONLY, env-gated; see coop/dev/director/director.h.
//
// The host picks a loose keyed prop near itself. Each peer in turn walks to it, takes it through the use key's grab
// chain and lets it go; both then read their copy's pose and compare. Each direction runs twice: as the copies fall,
// and with the other peer's copy pushed aside just after the release, so the two cannot land together by luck.
// Then each peer in turn runs the game's own tool_propProps_C on the prop through its init(toolgun), the call the
// toolgun makes, and the other peer reads the prop's flags after each step. The toolgun's aim and its menu are not
// run: the drill writes the prop into the toolgun's outHit and the tool's bools by hand. Greppable as "rest-tool:".
// Gated by VOTVCOOP_RUN_REST_TOOL; the peers' shared folder is VOTVCOOP_CAN_DIR. VOTVCOOP_REST_TOOL_KEY pins the
// target to a prop's key.

#include "coop/dev/director/aimed_grab.h"
#include "coop/dev/director/director.h"

#include "coop/config/config.h"
#include "coop/dev/drill_hand.h"
#include "coop/player/hand_item.h"
#include "coop/player/players_registry.h"
#include "coop/props/prop_hold_claim.h"
#include "coop/props/prop_key_index.h"
#include "coop/session/net_pump.h"
#include "harness/autotest.h"

#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/prop_gather.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/trace.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/engine/hit_result.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace coop::director {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace GT = ue_wrap::game_thread;

constexpr float kReachCm   = 140.f;
constexpr float kPickMinCm = 250.f;
constexpr float kPickMaxCm = 1500.f;
constexpr float kIsolationCm = 60.f;
constexpr float kPickAboveCm = 10.f;   // a target no higher than this above the player's origin
constexpr float kPushCm    = 45.f;    // the other peer's copy, pushed aside mid-fall
constexpr float kSameCm    = 2.f;     // the settle's send epsilons on three axes, and a little
constexpr float kLiftPitchDeg = 40.f;  // the dropper looks up, and its hand lifts the prop

std::string g_dir;
bool g_isHost = false;
std::wstring g_key;

template <class Fn>
void OnGameThread(Fn&& fn) {
    GT::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Mark(const std::string& name, const std::string& body = "x") {
    std::ofstream(g_dir + "\\" + name, std::ios::binary | std::ios::trunc) << body;
}

bool Await(const std::string& name, std::string* body = nullptr, int seconds = 300) {
    for (int i = 0; i < seconds * 10; ++i) {
        std::ifstream f(g_dir + "\\" + name, std::ios::binary);
        std::string s;
        if (f && std::getline(f, s) && !s.empty()) {
            if (body) *body = s;
            return true;
        }
        ::Sleep(100);
    }
    UE_LOGW("rest-tool: VERDICT ABORT -- no '%hs' from the other peer", name.c_str());
    return false;
}

void* Player() { return coop::players::Registry::Get().Local(); }
void* Prop() { return coop::prop_key_index::Find(g_key); }

bool Isolated(void* a, const ue_wrap::FVector& p, const std::vector<coop::prop_key_index::DebugEntry>& all) {
    for (const auto& o : all) {
        if (o.actor == a || !o.actor || !R::IsLive(o.actor)) continue;
        ue_wrap::FVector q{};
        if (!E::TryGetActorLocation(o.actor, q)) continue;
        if (std::fabs(q.X - p.X) < kIsolationCm && std::fabs(q.Y - p.Y) < kIsolationCm &&
            std::fabs(q.Z - p.Z) < kIsolationCm)
            return false;
    }
    return true;
}

// A player standing 1 m from the prop, on the host's side, sees it: the line from its eye (kEyeAboveCm over the host's
// origin) to the prop's bounds centre is clear. A prop inside a closed locker or behind a bunk fails it, and a player's
// aim could not take it.
constexpr float kEyeAboveCm = 70.f;
bool SeenFromStanding(void* player, void* prop, const ue_wrap::FVector& me) {
    ue_wrap::FVector c{}, ext{};
    if (!E::GetActorBounds(prop, /*onlyColliding=*/true, c, ext) && !E::TryGetActorLocation(prop, c)) return false;
    float dx = me.X - c.X, dy = me.Y - c.Y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.f) return false;
    dx /= len;
    dy /= len;
    const ue_wrap::FVector eye{c.X + dx * 100.f, c.Y + dy * 100.f, me.Z + kEyeAboveCm};
    // The trace's set is level geometry and dynamic actors, not physics bodies: a prop never takes it, so a clear line
    // to its centre is what a standing player's view of it is.
    return ue_wrap::trace::LineBlockedStatDyn(player, eye, c) == 0;
}

// HOST: the nearest loose keyed prop a hand can hold, at walking distance, with no neighbour at hand. Game thread.
bool PickTarget() {
    void* const player = Player();
    ue_wrap::FVector me{};
    if (!player || !E::TryGetActorLocation(player, me)) return false;
    std::vector<coop::prop_key_index::DebugEntry> all;
    coop::prop_key_index::DebugCollect(all);
    float bestD = 1e30f;
    for (const auto& e : all) {
        void* a = e.actor;
        if (!a || !R::IsLive(a) || !coop::prop_hold_claim::IsArbitrated(a)) continue;
        if (ue_wrap::prop::IsFrozen(a) || ue_wrap::prop::IsStatic(a) || ue_wrap::prop::IsHeavy(a)) continue;
        ue_wrap::prop_gather::HoldFacts hf{};
        if (!ue_wrap::prop_gather::ReadHoldFacts(a, hf) || hf.child || !hf.canHold || hf.heavy || hf.pickupRefused)
            continue;
        ue_wrap::FVector p{};
        if (!E::TryGetActorLocation(a, p)) continue;
        const float d = std::sqrt((p.X - me.X) * (p.X - me.X) + (p.Y - me.Y) * (p.Y - me.Y) +
                                  (p.Z - me.Z) * (p.Z - me.Z));
        // Below the waist (the player's origin is its capsule's centre): a prop at eye height, on a bunk or a shelf,
        // lies along the player's trace and the mattress or shelf under it takes the trace instead.
        if (d < kPickMinCm || d > kPickMaxCm || p.Z - me.Z > kPickAboveCm || me.Z - p.Z > 150.f) continue;
        if (d < bestD && Isolated(a, p, all) && SeenFromStanding(player, a, me)) { bestD = d; g_key = e.key; }
    }
    return !g_key.empty();
}

// ---- Where a dropped prop rests ------------------------------------------------------------------------------

// The owner's half: to the prop, into the hand, and let go.
bool GrabAndDrop(const std::string& arm) {
    ue_wrap::FVector at{};
    void* prop = nullptr;
    void* player = nullptr;
    OnGameThread([&] { player = Player(); prop = Prop(); if (prop) E::TryGetActorLocation(prop, at); });
    if (!player || !prop) { UE_LOGW("rest-tool: %hs VERDICT ABORT -- no player or prop", arm.c_str()); return false; }
    DirectorGoal goal;
    goal.reachCm = kReachCm;
    goal.targetActor = prop;
    goal.targetPos = at;
    {
        ControlManager mgr;
        AddWalkToProcesses(mgr, goal);
        mgr.Run(goal, /*maxSeconds=*/120);
    }
    if (!goal.reached) {
        UE_LOGW("rest-tool: %hs VERDICT ABORT -- did not reach the prop (%hs)", arm.c_str(), goal.failReason);
        return false;
    }
    AimedGrab grab(player, prop);
    GrabState state = GrabState::Working;
    for (int i = 0; i < 1200 && state == GrabState::Working; ++i) {
        OnGameThread([&] { state = grab.Tick(); });
        ::Sleep(16);
    }
    if (state != GrabState::Grabbed) {
        UE_LOGW("rest-tool: %hs VERDICT ABORT -- the grab did not take (%hs)", arm.c_str(), grab.Why());
        return false;
    }
    // The hand follows the view: looking up lifts the prop, so the release is a real fall. The other peer's copy
    // follows the hand.
    const ULONGLONG liftEnd = ::GetTickCount64() + 2500;
    while (::GetTickCount64() < liftEnd) {
        OnGameThread([&] {
            void* const c = E::GetController(player);
            if (c) { ue_wrap::FRotator r = E::GetControlRotation(c); r.Pitch = kLiftPitchDeg; E::SetControlRotation(c, r); }
        });
        ::Sleep(16);
    }
    bool released = false;
    ue_wrap::FVector from{};
    OnGameThread([&] {
        void* p = Prop();
        if (p) E::TryGetActorLocation(p, from);
        released = p && E::ReleaseMainPlayerGrabIfHolding(player, p);
    });
    UE_LOGI("rest-tool: %hs let go at (%.1f, %.1f, %.1f), %.1f cm above where it was picked up (released=%d)",
            arm.c_str(), from.X, from.Y, from.Z, from.Z - at.Z, released ? 1 : 0);
    return released;
}

std::string PoseText() {
    ue_wrap::FVector l{};
    ue_wrap::FRotator r{};
    OnGameThread([&] {
        void* p = Prop();
        if (p) { E::TryGetActorLocation(p, l); E::TryGetActorRotation(p, r); }
    });
    return std::to_string(l.X) + " " + std::to_string(l.Y) + " " + std::to_string(l.Z) + " " +
           std::to_string(r.Pitch) + " " + std::to_string(r.Yaw) + " " + std::to_string(r.Roll);
}

float AxisDelta(float a, float b) {
    float d = std::fmod(a - b, 360.f);
    if (d > 180.f) d -= 360.f;
    if (d < -180.f) d += 360.f;
    return std::fabs(d);
}

void CompareRests(const std::string& arm, bool iOwn) {
    const std::string mine = PoseText();
    const char* const me = g_isHost ? "host" : "client";
    const char* const other = g_isHost ? "client" : "host";
    Mark(arm + "_rest_" + me, mine);
    std::string theirs;
    if (!Await(arm + "_rest_" + other, &theirs)) return;
    float a[6]{}, b[6]{};
    std::sscanf(mine.c_str(), "%f %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
    std::sscanf(theirs.c_str(), "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    const float dist = std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                                 (a[2] - b[2]) * (a[2] - b[2]));
    const float deg = (std::max)({AxisDelta(a[3], b[3]), AxisDelta(a[4], b[4]), AxisDelta(a[5], b[5])});
    UE_LOGI("rest-tool: %hs %hs(%hs) at (%.1f, %.1f, %.1f); the copies are %.2f cm and %.2f deg apart -- %hs",
            arm.c_str(), me, iOwn ? "dropper" : "other", a[0], a[1], a[2], dist, deg,
            dist <= kSameCm ? "PASS" : "FAIL");
}

void DropArm(const std::string& arm, bool iOwn, bool push) {
    if (iOwn) {
        if (!GrabAndDrop(arm)) { Mark(arm + "_abort"); return; }
        Mark(arm + "_released");
    } else {
        if (!Await(arm + "_released")) return;
        if (push) {
            OnGameThread([&] {
                void* p = Prop();
                ue_wrap::FVector l{};
                if (p && E::TryGetActorLocation(p, l)) {
                    l.X += kPushCm;
                    E::SetActorLocation(p, l);
                    UE_LOGI("rest-tool: %hs pushed this copy aside by %.0f cm", arm.c_str(), kPushCm);
                }
            });
        }
    }
    ::Sleep(9000);   // the fall, the rest time and the crossing
    CompareRests(arm, iOwn);
    Mark(arm + std::string("_read_") + (g_isHost ? "host" : "client"));
    if (!Await(arm + std::string("_read_") + (g_isHost ? "client" : "host"))) return;
}

// ---- A toolgun's flags -----------------------------------------------------------------------------------------

struct Flags { bool isStatic, frozen, sleeping; const char* name; };
const Flags kSteps[] = {
    {false, true,  false, "freeze"},
    {false, false, false, "unfreeze"},
    {true,  false, false, "make static"},
    {false, false, false, "not static"},
    {false, false, false, "no change (control)"},
    {false, false, true,  "sleep"},
    {false, false, false, "wake"},
};

void SetBool(void* obj, const wchar_t* name, bool v) {
    int32_t byte = -1;
    uint8_t mask = 0;
    if (!R::FindBoolProperty(R::ClassOf(obj), name, byte, mask) || byte < 0) {
        UE_LOGW("rest-tool: the tool has no bool %ls", name);
        return;
    }
    uint8_t& b = static_cast<uint8_t*>(obj)[byte];
    b = v ? static_cast<uint8_t>(b | mask) : static_cast<uint8_t>(b & ~mask);
}

void* g_toolgun = nullptr;

// The game's own tool on the prop: spawned, its bools set, its init called with a toolgun whose outHit names the prop.
void RunTool(const Flags& f) {
    OnGameThread([&] {
        void* const player = Player();
        void* const prop = Prop();
        ue_wrap::FVector at{};
        if (!player || !prop || !E::TryGetActorLocation(prop, at)) { UE_LOGW("rest-tool: no prop for the tool"); return; }
        // The toolgun in this player's hand: the host judges a client's press only from a hand that holds one.
        void* const hand = coop::hand_item::LocalHandActor();
        g_toolgun = hand && R::ClassNameOf(hand) == L"prop_toolgun_C" ? hand : nullptr;
        void* const toolCls = R::FindClass(L"tool_propProps_C");
        void* const tool = toolCls ? E::SpawnActor(toolCls, at) : nullptr;
        void* const initFn = tool ? R::FindFunction(R::ClassOf(tool), L"Init") : nullptr;
        if (!g_toolgun || !tool || !initFn) {
            UE_LOGW("rest-tool: toolgun=%p tool=%p init=%p -- the tool cannot run", g_toolgun, tool, initFn);
            return;
        }
        if (!ue_wrap::hit_result::WriteField(g_toolgun, L"OutHit", prop, ue_wrap::prop::GetStaticMesh(prop), at)) {
            UE_LOGW("rest-tool: the toolgun's outHit did not take the prop");
            return;
        }
        SetBool(tool, L"isStatic", f.isStatic);
        SetBool(tool, L"isFrozen", f.frozen);
        SetBool(tool, L"isActive", true);
        SetBool(tool, L"isSleeping", f.sleeping);
        ue_wrap::ParamFrame frame(initFn);
        frame.Set<void*>(L"toolgun", g_toolgun);
        const bool ok = frame.valid() && ue_wrap::Call(tool, frame);
        UE_LOGI("rest-tool: tool '%hs' ran (ok=%d)", f.name, ok ? 1 : 0);
    });
}

void JudgeFlags(const char* who, const std::string& step, const Flags& f) {
    bool s = false, z = false, sl = false, have = false;
    OnGameThread([&] {
        void* p = Prop();
        if (p) { have = true; s = ue_wrap::prop::IsStatic(p); z = ue_wrap::prop::IsFrozen(p); sl = ue_wrap::prop::IsSleeping(p); }
    });
    const bool pass = have && s == f.isStatic && z == f.frozen && sl == f.sleeping;
    UE_LOGI("rest-tool: %hs step '%hs' %hs reads static=%d frozen=%d sleep=%d, wanted %d %d %d -- %hs", step.c_str(), f.name,
            who, s ? 1 : 0, z ? 1 : 0, sl ? 1 : 0, f.isStatic ? 1 : 0, f.frozen ? 1 : 0, f.sleeping ? 1 : 0,
            pass ? "PASS" : "FAIL");
}

// The toolgun taken into this player's hand as a player takes one, and held until the other peer can see it there.
bool ToolgunInHand() {
    OnGameThread([] { ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/prop_toolgun.prop_toolgun_C"); });
    OnGameThread([] { coop::dev::drill_hand::TakeIntoHandNow(L"toolgun", L"prop_toolgun_C"); });
    for (int i = 0; i < 100; ++i) {
        bool in = false;
        OnGameThread([&] {
            void* h = coop::hand_item::LocalHandActor();
            in = h && R::ClassNameOf(h) == L"prop_toolgun_C";
        });
        if (in) {
            ::Sleep(3000);  // the hand's item reaches the other peer
            UE_LOGI("rest-tool: the toolgun is in this player's hand");
            return true;
        }
        ::Sleep(100);
    }
    UE_LOGW("rest-tool: the toolgun did not reach this player's hand");
    return false;
}

void ToolArm(const std::string& arm, bool iOwn) {
    if (iOwn && !ToolgunInHand()) return;
    const int n = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));
    for (int i = 0; i < n; ++i) {
        const std::string step = arm + "_" + std::to_string(i);
        if (iOwn) {
            RunTool(kSteps[i]);
            ::Sleep(300);
            JudgeFlags("user", step, kSteps[i]);
            Mark(step + "_ran");
            if (!Await(step + "_read")) return;
        } else {
            if (!Await(step + "_ran")) return;
            ::Sleep(1000);
            JudgeFlags("other", step, kSteps[i]);
            Mark(step + "_read");
        }
    }
}

}  // namespace

DWORD WINAPI RestToolThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("rest-tool: VERDICT ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    g_isHost = !harness::autotest::IsClientRole();
    if (g_isHost) {
        if (!Await("client_ready")) return 0;
        bool picked = false;
        // VOTVCOOP_REST_TOOL_KEY pins the target to a known prop instead of the nearest one that fits.
        if (const std::string pin = coop::config::ReadEnv("VOTVCOOP_REST_TOOL_KEY"); !pin.empty()) {
            g_key.assign(pin.begin(), pin.end());
            picked = true;
        } else {
            OnGameThread([&] { picked = PickTarget(); });
        }
        if (!picked) { UE_LOGW("rest-tool: VERDICT ABORT -- no holdable keyed prop in reach of a walk"); return 0; }
        Mark("target", std::string(g_key.begin(), g_key.end()));
    } else {
        bool ready = false;
        for (int i = 0; i < 480 && !ready; ++i) {
            OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
            if (!ready) ::Sleep(500);
        }
        ::Sleep(15000);
        Mark("client_ready");
        std::string k;
        if (!Await("target", &k)) return 0;
        g_key.assign(k.begin(), k.end());
    }
    bool found = false;
    OnGameThread([&] { found = Prop() != nullptr; });
    UE_LOGI("rest-tool: target '%ls' found=%d", g_key.c_str(), found ? 1 : 0);
    if (!found) { UE_LOGW("rest-tool: VERDICT ABORT -- the target names no prop here"); return 0; }

    DropArm("drop-client-falls", !g_isHost, false);
    DropArm("drop-host-falls", g_isHost, false);
    DropArm("drop-client-pushed", !g_isHost, true);
    DropArm("drop-host-pushed", g_isHost, true);
    ToolArm("tool-client", !g_isHost);
    ToolArm("tool-host", g_isHost);
    ::Sleep(1000);
    UE_LOGI("rest-tool: DONE");
    return 0;
}

}  // namespace coop::director
