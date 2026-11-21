// coop/dev/lightsave_probe.cpp -- [dev] a client's lights through the host's save and its load. See the header.

#include "coop/dev/lightsave_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/devices/lightswitch.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace coop::dev::lightsave_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace LS = ue_wrap::lightswitch;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Mode() {
    static const std::string m = coop::config::ReadEnv("VOTVCOOP_LIGHTSAVE_PROBE");
    return m;
}

uint64_t g_startAt = 0;
int g_looks = 0, g_presses = 0;
bool g_flipDone = false, g_saved = false;
uint64_t g_nextPress = 0;

// Every switch with a light group, by switch key.
std::vector<std::pair<std::wstring, void*>> Switches() {
    std::vector<std::pair<std::wstring, void*>> out;
    if (!LS::EnsureSwitchResolved()) return out;
    for (void* sw : R::FindObjectsByClass(L"lightswitch_C")) {
        if (!sw || !R::IsLive(sw) || !LS::IsLightSwitch(sw) || !LS::SwitchRoot(sw)) continue;
        out.emplace_back(LS::GetSwitchKeyString(sw), sw);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

void Look(int slot, const char* when) {
    std::wstring line;
    int on = 0, n = 0;
    for (const auto& [key, sw] : Switches()) {
        void* const root = LS::SwitchRoot(sw);
        bool act = false, brk = false;
        LS::TryReadActive(root, act);
        LS::TryReadBreaker(root, brk);
        ++n;
        on += act ? 1 : 0;
        line += L" " + LS::GetKeyString(root) + (act ? L"=ON" : L"=off") + (brk ? L"" : L"(breaker)");
    }
    UE_LOGI("[LIGHTSAVE] %s slot=%d groups=%d on=%d:%ls", when, slot, n, on, line.c_str());
}

void Save() {
    void* const gm = ue_wrap::world_singleton::Gamemode();
    void* const fn = gm ? R::FindFunction(R::ClassOf(gm), L"save") : nullptr;
    bool ok = false;
    if (fn) {
        ue_wrap::ParamFrame f(fn);
        f.Set<bool>(L"quicksave", false);
        f.Set<bool>(L"bypassEvent", true);
        f.Set<bool>(L"overwriteSubsave", false);
        ok = f.valid() && ue_wrap::Call(gm, f);
    }
    UE_LOGI("[LIGHTSAVE] the host runs the game's save: fn=%p ok=%d", fn, ok ? 1 : 0);
}

}  // namespace

void Tick(coop::net::Session* session) {
    const std::string& mode = Mode();
    if ((mode != "flip" && mode != "look") || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[LIGHTSAVE] probe armed mode=%s slot=%d", mode.c_str(), slot);
    }
    const uint64_t t = Now() - g_startAt;
    static const uint64_t kLooks[] = {30000u, 70000u, 110000u};
    if (g_looks < 3 && t >= kLooks[g_looks]) {
        static const char* kWhen[] = {"at 30 s", "at 70 s", "at 110 s"};
        Look(slot, kWhen[g_looks]);
        ++g_looks;
    }
    if (mode != "flip") return;
    if (!host && slot == 1 && !g_flipDone && t >= 40000u && Now() >= g_nextPress) {
        g_nextPress = Now() + 3000u;
        for (const auto& [key, sw] : Switches()) {
            void* const root = LS::SwitchRoot(sw);
            bool act = true, brk = false;
            LS::TryReadActive(root, act);
            LS::TryReadBreaker(root, brk);
            if (act || !brk) continue;
            const bool ok = LS::CallUse(sw);
            bool after = false;
            LS::TryReadActive(root, after);
            ++g_presses;
            UE_LOGI("[LIGHTSAVE] slot 1 presses switch '%ls' (group '%ls'): ok=%d, its group reads %d", key.c_str(),
                    LS::GetKeyString(root).c_str(), ok ? 1 : 0, after ? 1 : 0);
            break;
        }
        if (g_presses >= 5) g_flipDone = true;
        for (const auto& [key, sw] : Switches()) {
            bool act = false;
            if (LS::TryReadActive(LS::SwitchRoot(sw), act) && act && g_presses > 0) { g_flipDone = true; break; }
        }
    }
    if (host && !g_saved && t >= 75000u) { g_saved = true; Save(); }
}

}  // namespace coop::dev::lightsave_probe
