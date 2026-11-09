// harness/autotest/autotest_crowbar.cpp -- TEST-ONLY: a crowbar breaking a prop, on the host and from a client.
//
// Each peer takes a crowbar into its hand, turns its view to the target and strikes it every 700 ms until it is gone
// or 40 strikes: by default the prop's own addDamage with the crowbar's damage, the call mainPlayer_C::attack ends in;
// with VOTVCOOP_CROWBAR_REAL the player's own `attack`, whose trace can miss a small prop. The target class is
// VOTVCOOP_CROWBAR_TARGET, else prop_emptyBottle_C (health 25, resistance 1). Three steps:
//   1. the host breaks a target of its own: the control that the class breaks at all;
//   2. the host spawns a target beside the client, the client strikes its copy, and it must leave the host's world;
//   3. the client stands beside the nearest save-loaded prop of the class and strikes it, and it must leave the host's.
// Each check logs `crowbar: PASS|FAIL <what>`, ending with `crowbar: DONE fails=N`. Gated by VOTVCOOP_RUN_CROWBAR;
// the folder is VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"
#include "harness/autotest/peer_signal.h"

#include "coop/dev/director/director.h"
#include "coop/dev/drill_hand.h"
#include "coop/element/registry.h"
#include "coop/player/players_registry.h"
#include "coop/props/prop_key_index.h"
#include "coop/session/net_pump.h"
#include "coop/session/teleport_client.h"
#include "ue_wrap/actors/int_objects.h"
#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/weapon_catalog.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <sstream>
#include <string>

namespace harness::autotest {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace PS = harness::autotest::peer_signal;
using PS::Await;
using PS::AwaitText;
using PS::Mark;
using PS::WaitFor;

constexpr int kMaxStrikes = 40;

int g_fails = 0;
void* g_targetCls = nullptr;

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Check(bool ok, const std::string& what) {
    if (ok) {
        UE_LOGI("crowbar: PASS %hs", what.c_str());
    } else {
        ++g_fails;
        UE_LOGW("crowbar: FAIL %hs", what.c_str());
    }
}

std::string Narrow(const std::wstring& w) {
    std::string out;
    for (wchar_t c : w) out += (c > 0 && c < 128) ? static_cast<char>(c) : '?';
    return out;
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::string FormatPoint(const ue_wrap::FVector& p) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.1f %.1f %.1f", p.X, p.Y, p.Z);
    return buf;
}

ue_wrap::FVector ParsePoint(const std::string& s) {
    ue_wrap::FVector p{};
    std::istringstream in(s);
    in >> p.X >> p.Y >> p.Z;
    return p;
}

ue_wrap::FVector Here() {
    ue_wrap::FVector at{};
    OnGameThread([&] {
        void* pawn = coop::players::Registry::Get().Local();
        if (pawn) E::TryGetActorLocation(pawn, at);
    });
    return at;
}

void* FindByKey(const std::string& key) {
    void* a = nullptr;
    OnGameThread([&] { a = coop::prop_key_index::Find(Widen(key)); });
    return a;
}

// The target class: VOTVCOOP_CROWBAR_TARGET, else the empty bottle, else a beer. A prop whose damage resistance is 0
// never breaks by an impact (the game's divide by zero is 0), whatever its health. Game thread.
void* TargetClass() {
    char want[96] = {};
    const DWORD n = ::GetEnvironmentVariableA("VOTVCOOP_CROWBAR_TARGET", want, sizeof want);
    const std::wstring first = (n > 0 && n < sizeof want) ? Widen(want) : L"prop_emptyBottle_C";
    for (const wchar_t* name : {first.c_str(), L"prop_snack_beer_C"})
        if (void* cls = R::FindClass(name)) return cls;
    return nullptr;
}

// A target's impact state, as the strikes change it: health, unbreakable and damage resistance; "gone" when it is not
// in this world.
std::string TargetState(const std::string& key) {
    std::string out = "gone";
    OnGameThread([&] {
        void* prop = coop::prop_key_index::Find(Widen(key));
        if (!prop) return;
        const int32_t offComp = R::FindPropertyOffset(R::ClassOf(prop), L"physicsImpact");
        void* comp = offComp >= 0 ? *reinterpret_cast<void**>(static_cast<uint8_t*>(prop) + offComp) : nullptr;
        if (!comp) {
            out = "no physicsImpact";
            return;
        }
        void* const cls = R::ClassOf(comp);
        const int32_t offH = R::FindPropertyOffset(cls, L"health");
        const float health = offH >= 0 ? *reinterpret_cast<float*>(static_cast<uint8_t*>(comp) + offH) : -1.f;
        int32_t offU = -1;
        uint8_t mask = 0;
        const bool unbreakable = R::FindBoolProperty(cls, L"unbreakable", offU, mask) &&
                                 (static_cast<uint8_t*>(comp)[offU] & mask) != 0;
        const int32_t offD = R::FindPropertyOffset(cls, L"damageData");
        void* st = R::PropertyInnerStruct(cls, L"damageData");
        const int32_t offR = st ? R::FindPropertyOffsetByPrefix(st, L"damageResistance_") : -1;
        const float res = offD >= 0 && offR >= 0 ? *reinterpret_cast<float*>(static_cast<uint8_t*>(comp) + offD + offR)
                                                 : -1.f;
        char buf[96];
        std::snprintf(buf, sizeof buf, "health=%.1f unbreakable=%d resistance=%.2f", health, unbreakable ? 1 : 0, res);
        out = buf;
    });
    return out;
}

// A target at `at`; its key once the game gave it one, "" on a failed spawn.
std::string SpawnTarget(const ue_wrap::FVector& at) {
    void* prop = nullptr;
    OnGameThread([&] { prop = g_targetCls ? E::SpawnActor(g_targetCls, at) : nullptr; });
    if (!prop) return "";
    std::string key;
    WaitFor("the target's key", 5, [&] {
        OnGameThread([&] {
            const std::wstring k = ue_wrap::prop::GetKeyString(prop);
            if (!k.empty() && k != L"None") key = Narrow(k);
        });
        return !key.empty();
    }, false);
    return key;
}

// The hand is emptied first: a join can restore a crowbar into it, and taking one over it leaves the hand slot's name
// empty, which the host reads as no swinging item and refuses every strike. True when the slot then names a crowbar.
bool TakeCrowbar() {
    OnGameThread([] { coop::dev::drill_hand::TakeIntoHandNow(L"None", nullptr); });
    ::Sleep(500);
    bool ok = false;
    OnGameThread([&] {
        ok = coop::dev::drill_hand::TakeIntoHandNow(L"crowbar", L"prop_crowbar_C") &&
             ue_wrap::inventory::ReadHeldItemName() == L"crowbar";
    });
    return ok;
}

// Turn the view to the target and strike it once. False when the call did not run.
bool StrikeAt(const std::string& key) {
    bool ok = false;
    OnGameThread([&] {
        void* prop = coop::prop_key_index::Find(Widen(key));
        void* me = coop::players::Registry::Get().Local();
        void* ctrl = me ? E::GetController(me) : nullptr;
        ue_wrap::FVector at{};
        if (!prop || !ctrl || !E::TryGetActorLocation(prop, at)) return;
        const ue_wrap::FVector cam = E::GetCameraLocation();
        E::SetControlRotation(ctrl, coop::director::LookAt(cam, at));
        static const bool real = ::GetEnvironmentVariableA("VOTVCOOP_CROWBAR_REAL", nullptr, 0) > 0;
        if (real) {
            void* fn = R::FindFunction(R::ClassOf(me), L"attack");
            if (!fn) return;
            ue_wrap::ParamFrame f(fn);
            ok = f.valid() && ue_wrap::Call(me, f);
            return;
        }
        // What attack passes the prop it strikes: this player the damager, the crowbar's damage, the struck point at
        // the prop, a shove along the view.
        ue_wrap::weapon_catalog::Swing sw;
        const float dmg = ue_wrap::weapon_catalog::Lookup(L"crowbar", sw) && sw.maxDamage > 0.f ? sw.maxDamage : 10.f;
        const ue_wrap::FVector shove{(at.X - cam.X) * 0.5f, (at.Y - cam.Y) * 0.5f, (at.Z - cam.Z) * 0.5f};
        ok = ue_wrap::int_objects::InvokeAddDamage(prop, me, dmg, at, shove);
    });
    return ok;
}

// Strikes until the target is gone here, or kMaxStrikes. The strikes that ran.
int StrikeUntilGone(const std::string& key, bool& gone) {
    int ran = 0;
    gone = false;
    for (int i = 0; i < kMaxStrikes && !gone; ++i) {
        if (StrikeAt(key)) ++ran;
        ::Sleep(700);
        if (i < 3 || i % 5 == 0) UE_LOGI("crowbar: strike %d: %hs", i + 1, TargetState(key).c_str());
        gone = FindByKey(key) == nullptr;
    }
    return ran;
}

void RunHost() {
    std::string clientAt;
    if (!AwaitText("client_ready", clientAt)) return;

    // 1. The control: the host breaks its own target.
    std::string clsName = "none";
    OnGameThread([&] {
        g_targetCls = TargetClass();
        if (g_targetCls) clsName = Narrow(R::ToString(R::NameOf(g_targetCls)));
    });
    Check(g_targetCls != nullptr, "1: the target class is loaded: " + clsName);
    const ue_wrap::FVector me = Here();
    const std::string a = SpawnTarget({me.X, me.Y + 150.f, me.Z});
    Check(!a.empty(), "1: the host spawned its control target (" + a + ")");
    Check(TakeCrowbar(), "1: the host holds a crowbar");
    ::Sleep(2000);
    bool goneA = false;
    const int ranA = a.empty() ? 0 : StrikeUntilGone(a, goneA);
    Check(goneA, "1: the host's own crowbar broke its target (" + std::to_string(ranA) + " strikes ran)");
    // Empty the hand so the host's crowbar is not in reach of the client's target below.
    OnGameThread([&] { coop::dev::drill_hand::TakeIntoHandNow(L"None", nullptr); });

    // 2. The target the client strikes, beside the client.
    const ue_wrap::FVector c = ParsePoint(clientAt);
    const std::string b = SpawnTarget({c.X + 150.f, c.Y, c.Z});
    Check(!b.empty(), "2: the host spawned the client's target (" + b + ")");
    Mark("target", b.empty() ? "none" : b);
    std::string result;
    if (!AwaitText("client_struck", result, 120)) return;
    const bool goneHere = !b.empty() && WaitFor("the client's target gone from the host", 10,
                                                [&] { return FindByKey(b) == nullptr; }, false);
    Check(goneHere, "2: the client's crowbar broke the target on the host (client says: " + result + ")");

    // 3. A prop of the class that came from the save: the client stands beside one and strikes it.
    Mark("world_go");
    std::string world;
    if (!AwaitText("world_struck", world, 120)) return;
    const std::string wkey = world.substr(0, world.find('|'));
    if (wkey != "none") {
        const bool goneW = WaitFor("the save's prop gone from the host", 10, [&] { return FindByKey(wkey) == nullptr; },
                                   false);
        Check(goneW, "3: the client's crowbar broke a save-loaded prop on the host (client says: " + world + ")");
    }
    Mark("host_done");
    UE_LOGI("crowbar: DONE fails=%d", g_fails);
}

// The key and place of the save-loaded prop of the target class nearest `here`; an empty key when none is keyed.
// Game thread.
void NearestOfClass(const ue_wrap::FVector& here, std::string& key, ue_wrap::FVector& at) {
    void* const cls = TargetClass();
    if (!cls) return;
    struct Ctx {
        ue_wrap::FVector here;
        float bestD = 1e30f;
        std::string* key;
        ue_wrap::FVector* at;
    } ctx{here, 1e30f, &key, &at};
    ue_wrap::object_index::ForEachInstance(cls, [](void* c, void* obj, int32_t) {
        auto* x = static_cast<Ctx*>(c);
        if (R::NameStartsWith(R::NameOf(obj), L"Default__")) return;
        const std::wstring k = ue_wrap::prop::GetKeyString(obj);
        ue_wrap::FVector p{};
        if (k.empty() || k == L"None" || !E::TryGetActorLocation(obj, p)) return;
        const float dx = p.X - x->here.X, dy = p.Y - x->here.Y, dz = p.Z - x->here.Z;
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < x->bestD) {
            x->bestD = d;
            *x->key = Narrow(k);
            *x->at = p;
        }
    }, &ctx);
}

void RunClient() {
    bool ready = false;
    for (int i = 0; i < 480 && !ready; ++i) {
        OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
        if (!ready) ::Sleep(500);
    }
    ::Sleep(10000);  // the join settles before the props are read
    Mark("client_ready", FormatPoint(Here()));

    std::string b;
    if (!AwaitText("target", b, 180)) return;
    if (b == "none") {
        Mark("client_struck", "no target");
        return;
    }
    const bool there = WaitFor("the host's target here", 15, [&] { return FindByKey(b) != nullptr; }, false);
    Check(there, "2: the host's target stands on the client (" + b + ")");
    Check(TakeCrowbar(), "2: the client holds a crowbar");
    ::Sleep(2000);
    bool gone = false;
    const int ran = there ? StrikeUntilGone(b, gone) : 0;
    Check(gone, "2: the client's crowbar broke its copy of the target (" + std::to_string(ran) + " strikes ran)");
    Mark("client_struck", std::string(gone ? "gone" : "standing") + " after " + std::to_string(ran) + " strikes");

    // 3. The nearest save-loaded prop of the class: the client stands beside it and strikes it.
    if (!Await("world_go", 120)) return;
    const ue_wrap::FVector here = Here();
    std::string wkey;
    uint32_t weid = 0;
    ue_wrap::FVector wat{};
    OnGameThread([&] {
        NearestOfClass(here, wkey, wat);
        if (void* a = wkey.empty() ? nullptr : coop::prop_key_index::Find(Widen(wkey)))
            weid = static_cast<uint32_t>(coop::element::Registry::Get().EidForActor(a));
    });
    if (wkey.empty()) {
        Mark("world_struck", "none|no save-loaded prop of the class");
    } else {
        UE_LOGI("crowbar: save-loaded target key=%hs eid=%u at %hs", wkey.c_str(), weid, FormatPoint(wat).c_str());
        OnGameThread([&] { coop::teleport_client::ApplyLocally({wat.X - 120.f, wat.Y, wat.Z + 60.f, 0.f, 0.f, 0.f}); });
        ::Sleep(2500);
        bool wgone = false;
        const int wran = StrikeUntilGone(wkey, wgone);
        Check(wgone, "3: the client's crowbar broke its copy of a save-loaded prop (eid " + std::to_string(weid) + ", " +
                         std::to_string(wran) + " strikes ran)");
        Mark("world_struck", wkey + "|" + (wgone ? "gone" : "standing") + " after " + std::to_string(wran) +
                                 " strikes, eid " + std::to_string(weid));
    }
    if (!Await("host_done", 60)) return;
    UE_LOGI("crowbar: DONE fails=%d", g_fails);
}

}  // namespace

DWORD WINAPI CrowbarThread(LPVOID /*arg*/) {
    if (!PS::Begin("crowbar")) return 0;
    if (IsClientRole()) RunClient();
    else                RunHost();
    return 0;
}

}  // namespace harness::autotest
