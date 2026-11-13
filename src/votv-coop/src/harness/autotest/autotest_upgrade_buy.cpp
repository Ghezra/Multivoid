// harness/autotest/autotest_upgrade_buy.cpp -- TEST-ONLY: the laptop's level upgrades, bought and sold by both peers.
//
// Each peer in turn enters the laptop and runs the panel row's own buy or sell button, and both peers must then read
// the same levels and balance, the ones the row's price table says. The runs cover the same row bought by both,
// different rows, a burst, the level cap, sells, too little money, a press without the claim and a press in the same
// tick as the exit. The physical upgrades are read at the start and the end, since a panel purchase has no reason to
// move them. Each step logs `upgrade-buy: <step> PASS|FAIL`. Gated by VOTVCOOP_RUN_UPGRADE_BUY; the peers' shared
// folder is VOTVCOOP_CAN_DIR. VOTVCOOP_UPGRADE_REJOIN=1 on a relaunched client compares a fresh join with the host.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/interactables/device_occupancy.h"
#include "coop/player/players_registry.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/component_calls.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/desk/device_screen.h"
#include "ue_wrap/devices/generator.h"
#include "ue_wrap/devices/laptop_widget.h"
#include "ue_wrap/devices/serverbox.h"
#include "ue_wrap/world/economy.h"
#include "ue_wrap/world/upgrades.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace harness::autotest {
namespace {

namespace DO = coop::device_occupancy;
namespace OI = ue_wrap::object_index;
namespace R  = ue_wrap::reflection;
namespace UP = ue_wrap::upgrades;

const wchar_t* const kClaim = L"laptop";
const wchar_t* const kBuy =
    L"BndEvt__button_upgDownloadSpd_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature";
const wchar_t* const kSell =
    L"BndEvt__button_upgDown_K2Node_ComponentBoundEvent_1_OnButtonClickedEvent__DelegateSignature";

// The rows the drill moves, by the panel's index and the struct's wire position (upgrades.cpp's table).
struct RowId { int panel; int wire; const char* name; };
constexpr RowId kA{5, 5, "coordPingSpeed"};
constexpr RowId kB{20, 12, "detecQual"};
constexpr RowId kC{7, 7, "processLvl"};
constexpr RowId kD{24, 14, "radar_speed"};

constexpr int32_t kStartPoints = 5000;
constexpr int32_t kPoorPoints  = 2;

struct State {
    int32_t level[UP::kLevelCount]{};
    int32_t points = 0;
};

std::string g_dir;
std::string g_me;
std::string g_other;
bool        g_isHost = false;
int         g_fails = 0;
State       g_model;

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Mark(const std::string& name) {
    std::ofstream(g_dir + "\\" + name, std::ios::binary | std::ios::trunc) << "x";
}

bool Await(const std::string& name, int seconds = 180) {
    for (int i = 0; i < seconds * 10; ++i) {
        std::ifstream f(g_dir + "\\" + name, std::ios::binary);
        std::string s;
        if (f && std::getline(f, s) && !s.empty()) return true;
        ::Sleep(100);
    }
    UE_LOGW("upgrade-buy: VERDICT ABORT -- no '%hs' from the other peer", name.c_str());
    return false;
}

bool Sync(int n) {
    Mark(g_me + "_" + std::to_string(n));
    return Await(g_other + "_" + std::to_string(n));
}

bool ReadState(State& out) {
    bool ok = false;
    OnGameThread([&] { ok = UP::ReadLevels(out.level) && ue_wrap::economy::ReadPoints(&out.points); });
    return ok;
}

bool Same(const State& a, const State& b) {
    if (a.points != b.points) return false;
    for (int i = 0; i < UP::kLevelCount; ++i)
        if (a.level[i] != b.level[i]) return false;
    return true;
}

std::string Text(const State& s) {
    std::ostringstream o;
    o << "A=" << s.level[kA.wire] << " B=" << s.level[kB.wire] << " C=" << s.level[kC.wire]
      << " D=" << s.level[kD.wire] << " points=" << s.points << " all=";
    for (int i = 0; i < UP::kLevelCount; ++i) o << s.level[i] << (i + 1 < UP::kLevelCount ? "," : "");
    return o.str();
}

void WriteState(const std::string& name, const State& s) {
    const std::string tmp = g_dir + "\\" + name + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << s.points;
        for (int i = 0; i < UP::kLevelCount; ++i) f << ' ' << s.level[i];
        f << '\n';
    }
    ::MoveFileExA(tmp.c_str(), (g_dir + "\\" + name).c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool ReadStateFile(const std::string& name, State& s) {
    std::ifstream f(g_dir + "\\" + name, std::ios::binary);
    if (!(f >> s.points)) return false;
    for (int i = 0; i < UP::kLevelCount; ++i)
        if (!(f >> s.level[i])) return false;
    return true;
}

// The model, in the row's own arithmetic: what a press should do, on both peers alike.
void ModelBuy(const RowId& r, int presses) {
    for (int i = 0; i < presses; ++i) {
        int32_t& lv = g_model.level[r.wire];
        if (lv >= UP::MaxLevel(r.panel) || g_model.points < UP::BasePrice(r.panel)) continue;
        g_model.points -= UP::PriceAtLevel(r.panel, lv);
        ++lv;
    }
}

void ModelSell(const RowId& r) {
    int32_t& lv = g_model.level[r.wire];
    if (lv <= 0) return;
    g_model.points += UP::RefundAtLevel(r.panel, lv);
    --lv;
}

// Both peers read the model and hold it for a second, so a later overshoot is not passed.
bool Settle(const char* step) {
    State now{};
    uint64_t matchedAt = 0;
    const uint64_t end = ::GetTickCount64() + 15000;
    while (::GetTickCount64() < end) {
        const bool match = ReadState(now) && Same(now, g_model);
        if (!match) matchedAt = 0;
        else if (matchedAt == 0) matchedAt = ::GetTickCount64();
        else if (::GetTickCount64() - matchedAt >= 1000) {
            UE_LOGI("upgrade-buy: %hs PASS on %hs %hs", step, g_me.c_str(), Text(now).c_str());
            return true;
        }
        ::Sleep(100);
    }
    ++g_fails;
    UE_LOGW("upgrade-buy: %hs FAIL on %hs read %hs", step, g_me.c_str(), Text(now).c_str());
    UE_LOGW("upgrade-buy: %hs expected %hs", step, Text(g_model).c_str());
    return false;
}

void* FindRow(int panelIndex) {
    void* const cls = R::FindClass(L"uicomp_upgradeSlot_C");
    if (!cls) return nullptr;
    const int32_t offIndex = R::FindPropertyOffset(cls, L"index");
    int32_t modByte = -1;
    uint8_t modMask = 0;
    if (offIndex < 0 || !R::FindBoolProperty(cls, L"module", modByte, modMask)) return nullptr;
    const int32_t offPrice = R::FindPropertyOffset(cls, L"price");
    struct Ctx { int index; int32_t off; int32_t offPrice; int32_t price; int32_t modByte; uint8_t modMask; void* found; int matches; };
    Ctx ctx{panelIndex, offIndex, offPrice, UP::BasePrice(panelIndex), modByte, modMask, nullptr, 0};
    OI::ForEachInstance(cls, [](void* c, void* obj, int32_t) {
        auto* x = static_cast<Ctx*>(c);
        if (!obj || R::NameStartsWith(R::NameOf(obj), L"Default__")) return;
        const auto* base = static_cast<const uint8_t*>(obj);
        if (*reinterpret_cast<const int32_t*>(base + x->off) != x->index) return;
        if (base[x->modByte] & x->modMask) return;
        const int32_t price = x->offPrice >= 0 ? *reinterpret_cast<const int32_t*>(base + x->offPrice) : -1;
        UE_LOGI("upgrade-buy: row index=%d price=%d (the table says %d) at %p", x->index, price, x->price, obj);
        if (price != x->price) return;
        ++x->matches;
        if (!x->found) x->found = obj;
    }, &ctx);
    if (ctx.matches != 1) UE_LOGW("upgrade-buy: panel index %d has %d level rows", panelIndex, ctx.matches);
    return ctx.found;
}

bool Press(const RowId& r, const wchar_t* button) {
    bool ok = false;
    OnGameThread([&] {
        void* const row = FindRow(r.panel);
        ok = row && ue_wrap::component_calls::CallParamlessNamed(row, button);
    });
    if (!ok) UE_LOGW("upgrade-buy: the press on %hs did not dispatch", r.name);
    return ok;
}

void PressMany(const RowId& r, const wchar_t* button, int presses) {
    for (int i = 0; i < presses; ++i) {
        Press(r, button);
        ::Sleep(50);
    }
}

// The way the game's enter ends: the laptop's widget becomes the player's active interface, and the occupancy poll
// claims it from that edge. No zoom: `zoom` sets the player's isLookAt, which leaves at once when nothing is aimed at.
bool SetActive(void* player, void* widget) {
    void* const fn = R::FindDispatchFunctionCached(R::ClassOf(player), L"setActiveInterface");
    if (!fn) return false;
    ue_wrap::ParamFrame f(fn);
    if (!f.valid()) return false;
    f.Set<void*>(L"activeInterface", widget);
    f.Set<bool>(L"isActiveINterface3D", true);
    return ue_wrap::Call(player, f);
}

// The game sends a player whose camera crossed a static object since the last tick to the backrooms, which ends a
// session. It fired on this drill's first run, so the run turns the check off on its own peer. Nothing here is saved.
void DisableBackrooms() {
    OnGameThread([] {
        void* const gm = ue_wrap::world_singleton::Gamemode();
        int32_t byte = -1;
        uint8_t mask = 0;
        if (gm && R::FindBoolProperty(R::ClassOf(gm), L"backroomsEnabled", byte, mask) && byte >= 0)
            static_cast<uint8_t*>(gm)[byte] &= static_cast<uint8_t>(~mask);
        else UE_LOGW("upgrade-buy: backroomsEnabled did not resolve");
    });
}

uint8_t Holder() {
    uint8_t h = 0xFF;
    OnGameThread([&] { h = DO::HolderOf(kClaim); });
    return h;
}

bool Enter() {
    for (int i = 0; i < 100 && Holder() != 0xFF; ++i) ::Sleep(100);
    bool called = false;
    OnGameThread([&] {
        void* const me = coop::players::Registry::Get().Local();
        void* const widget = ue_wrap::laptop_widget::Widget();
        called = me && widget && SetActive(me, widget);
    });
    if (!called) UE_LOGW("upgrade-buy: the enter did not dispatch on %hs", g_me.c_str());
    for (int i = 0; called && i < 100; ++i) {
        bool held = false;
        OnGameThread([&] { held = DO::LocalHolds(kClaim) && DO::HolderOf(kClaim) != 0xFF; });
        if (held) return true;
        ::Sleep(100);
    }
    ++g_fails;
    UE_LOGW("upgrade-buy: %hs never held the laptop claim", g_me.c_str());
    return false;
}

void Exit() {
    OnGameThread([&] { ue_wrap::device_screen::ForceExitInterface(coop::players::Registry::Get().Local()); });
    for (int i = 0; i < 100 && Holder() != 0xFF; ++i) ::Sleep(100);
}

// One turn: the actor enters and presses, both peers settle on the model, the actor leaves.
bool Turn(int n, bool hostActs, const char* step, const std::function<void()>& press,
          const std::function<void()>& model) {
    const bool mine = hostActs == g_isHost;
    if (mine && Enter()) press();
    model();
    Settle(step);
    if (mine) Exit();
    return Sync(n);
}

// What the game re-derives from the struct: the desk copies the radar speed in upgraded_pcUpgrades.
void CheckDerived(const char* step) {
    int32_t value = -1;
    bool read = false;
    OnGameThread([&] {
        void* const cls = R::FindClass(L"analogDScreenTest_C");
        const int32_t off = cls ? R::FindPropertyOffset(cls, L"radar_spdLevel") : -1;
        if (off < 0) return;
        struct Ctx { void* found; } ctx{nullptr};
        OI::ForEachInstance(cls, [](void* c, void* obj, int32_t) {
            auto* x = static_cast<Ctx*>(c);
            if (obj && !x->found && !R::NameStartsWith(R::NameOf(obj), L"Default__")) x->found = obj;
        }, &ctx);
        if (!ctx.found) return;
        value = *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(ctx.found) + off);
        read = true;
    });
    const int32_t want = g_model.level[kD.wire];
    const bool pass = read && value == want;
    if (!pass) ++g_fails;
    UE_LOGI("upgrade-buy: %hs derived %hs on %hs radar_spdLevel=%d want %d read=%d", step, pass ? "PASS" : "FAIL",
            g_me.c_str(), value, want, read ? 1 : 0);
}

// The physical upgrades: each generator's level and each server box's count.
std::string ReadPhysical() {
    std::string out;
    OnGameThread([&] {
        std::vector<void*> gens;
        ue_wrap::generator::ReadGenerators(gens);
        std::ostringstream o;
        o << "generators";
        for (void* g : gens) {
            ue_wrap::generator::Row row{};
            o << ' ' << (g && ue_wrap::generator::ReadRow(g, row) ? row.upgradeLevel : -1);
        }
        struct Ctx { int boxes; int64_t sum; } ctx{0, 0};
        if (void* const cls = R::FindClass(ue_wrap::serverbox::kBoxClass)) {
            OI::ForEachInstance(cls, [](void* c, void* obj, int32_t) {
                auto* x = static_cast<Ctx*>(c);
                if (!obj || R::NameStartsWith(R::NameOf(obj), L"Default__")) return;
                int32_t level = 0;
                if (ue_wrap::serverbox::ReadUpgrades(obj, level)) { ++x->boxes; x->sum += level; }
            }, &ctx);
        }
        o << " | server boxes " << ctx.boxes << " total upgrades " << ctx.sum;
        out = o.str();
    });
    return out;
}

void WaitJoined() {
    bool ready = false;
    for (int i = 0; i < 480 && !ready; ++i) {
        OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
        if (!ready) ::Sleep(500);
    }
    ::Sleep(10000);
}

void RunRejoin() {
    WaitJoined();
    State host{};
    if (!Await("final", 30) || !ReadStateFile("final", host)) return;
    g_model = host;
    Settle("rejoin the levels and balance");
    UE_LOGI("upgrade-buy: REJOIN DONE fails=%d", g_fails);
}

void RunSteps() {
    const std::string physical0 = ReadPhysical();
    UE_LOGI("upgrade-buy: physical at the start on %hs: %hs", g_me.c_str(), physical0.c_str());

    Turn(1, true, "01 host buys A twice", [] { PressMany(kA, kBuy, 2); }, [] { ModelBuy(kA, 2); });
    Turn(2, false, "02 client buys the same row twice", [] { PressMany(kA, kBuy, 2); }, [] { ModelBuy(kA, 2); });
    Turn(3, true, "03 host buys B", [] { Press(kB, kBuy); }, [] { ModelBuy(kB, 1); });
    Turn(4, false, "04 client buys D", [] { Press(kD, kBuy); }, [] { ModelBuy(kD, 1); });
    CheckDerived("04 client buys D");
    Turn(5, true, "05 host buys D", [] { Press(kD, kBuy); }, [] { ModelBuy(kD, 1); });
    CheckDerived("05 host buys D");
    Turn(6, false, "06 client presses A four times fast", [] { PressMany(kA, kBuy, 4); }, [] { ModelBuy(kA, 4); });
    Turn(7, false, "07 client presses C five times against its cap of 3", [] { PressMany(kC, kBuy, 5); },
         [] { ModelBuy(kC, 5); });
    Turn(8, true, "08 host presses C at its cap", [] { Press(kC, kBuy); }, [] { ModelBuy(kC, 1); });
    Turn(9, false, "09 client sells A", [] { Press(kA, kSell); }, [] { ModelSell(kA); });
    Turn(10, true, "10 host sells A", [] { Press(kA, kSell); }, [] { ModelSell(kA); });

    if (g_isHost) OnGameThread([] { ue_wrap::economy::WritePoints(kPoorPoints); });
    g_model.points = kPoorPoints;
    Settle("11a the balance drops below B's price");
    Sync(11);
    Turn(12, false, "12 client presses B without the money", [] { Press(kB, kBuy); }, [] { ModelBuy(kB, 1); });
    Turn(13, true, "13 host presses B without the money", [] { Press(kB, kBuy); }, [] { ModelBuy(kB, 1); });
    if (g_isHost) OnGameThread([] { ue_wrap::economy::WritePoints(kStartPoints); });
    g_model.points = kStartPoints;
    Settle("14 the balance is put back");
    Sync(14);

    // A client's press without the claim: the host holds the laptop, so the host refuses it.
    if (g_isHost) Enter();
    Sync(15);
    if (!g_isHost) Press(kA, kBuy);
    Settle("16 client presses A while the host holds the laptop");
    if (g_isHost) Exit();
    Sync(16);

    // The press and the leave in one tick: the release can reach the host before the queued purchase is served.
    if (!g_isHost && Enter()) {
        OnGameThread([] {
            void* const row = FindRow(kA.panel);
            if (row) ue_wrap::component_calls::CallParamlessNamed(row, kBuy);
            ue_wrap::device_screen::ForceExitInterface(coop::players::Registry::Get().Local());
        });
    }
    ModelBuy(kA, 1);
    Settle("17 client presses A and leaves in the same tick");
    Sync(17);

    const std::string physical1 = ReadPhysical();
    const bool same = physical0 == physical1;
    if (!same) ++g_fails;
    UE_LOGI("upgrade-buy: physical at the end on %hs %hs: %hs", g_me.c_str(), same ? "PASS" : "FAIL", physical1.c_str());
    std::ofstream(g_dir + "\\physical_" + g_me, std::ios::binary | std::ios::trunc) << physical1 << '\n';
    Sync(18);
    std::ifstream f(g_dir + "\\physical_" + g_other, std::ios::binary);
    std::string theirs;
    std::getline(f, theirs);
    const bool agree = theirs == physical1;
    if (!agree) ++g_fails;
    UE_LOGI("upgrade-buy: physical across peers %hs", agree ? "PASS" : "FAIL");

    State final{};
    if (ReadState(final) && g_isHost) WriteState("final", final);
    UE_LOGI("upgrade-buy: DONE on %hs fails=%d", g_me.c_str(), g_fails);
}

}  // namespace

DWORD WINAPI UpgradeBuyThread(LPVOID) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("upgrade-buy: VERDICT ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    g_isHost = !IsClientRole();
    g_me = g_isHost ? "host" : "client";
    g_other = g_isHost ? "client" : "host";
    if (!g_isHost && coop::config::ReadEnv("VOTVCOOP_UPGRADE_REJOIN") == "1") {
        RunRejoin();
        return 0;
    }
    if (g_isHost) {
        if (!Await("client_ready", 300)) return 0;
        DisableBackrooms();
        OnGameThread([] {
            for (const RowId& r : {kA, kB, kC, kD}) UP::WriteLevel(r.panel, 0);
            ue_wrap::economy::WritePoints(kStartPoints);
        });
        ::Sleep(1500);
        if (!ReadState(g_model)) { UE_LOGW("upgrade-buy: VERDICT ABORT -- the levels did not read"); return 0; }
        WriteState("base", g_model);
    } else {
        WaitJoined();
        DisableBackrooms();
        Mark("client_ready");
        if (!Await("base", 30) || !ReadStateFile("base", g_model)) return 0;
    }
    Settle("00 both start from the host's levels and balance");
    if (Sync(0)) RunSteps();
    return 0;
}

}  // namespace harness::autotest
