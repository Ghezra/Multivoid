// harness/autotest/autotest_info_hint.cpp -- TEST-ONLY: what each peer sees of a tip the game shows the other.
//
// The host, then the client, runs the game's own lib_C.addHint once for each notify type, labelled with the peer and
// the type. Both peers capture their windows straight after, so the captures show which type draws the blue (i) and
// whether a tip reaches the other peer. Gated by VOTVCOOP_RUN_INFO_HINT; the peers' shared folder is
// VOTVCOOP_CAN_DIR. Greppable as "info-hint:".

#include "harness/autotest.h"
#include "harness/screenshot.h"

#include "coop/config/config.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/ftext_utils.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>

namespace harness::autotest {
namespace {

namespace R = ue_wrap::reflection;

std::string g_dir;

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Mark(const std::string& name) {
    std::ofstream(g_dir + "\\" + name, std::ios::binary | std::ios::trunc) << "x";
}

bool Await(const std::string& name, int seconds = 240) {
    for (int i = 0; i < seconds * 10; ++i) {
        std::ifstream f(g_dir + "\\" + name, std::ios::binary);
        std::string s;
        if (f && std::getline(f, s) && !s.empty()) return true;
        ::Sleep(100);
    }
    UE_LOGW("info-hint: VERDICT ABORT -- no '%hs' from the other peer", name.c_str());
    return false;
}

void Hint(const std::wstring& who, uint8_t type) {
    OnGameThread([&] {
        void* const gm = ue_wrap::world_singleton::Gamemode();
        void* const lib = R::FindClassDefaultObject(L"lib_C");
        void* const fn = lib ? R::FindFunction(R::ClassOf(lib), L"addHint") : nullptr;
        if (!gm || !fn) { UE_LOGW("info-hint: addHint did not resolve"); return; }
        const std::wstring text = who + L" tip, type " + std::to_wstring(type);
        uint8_t ftext[ue_wrap::ftext_utils::kFTextSize]{};
        if (!ue_wrap::ftext_utils::MintFText(text.c_str(), ftext)) { UE_LOGW("info-hint: no text"); return; }
        ue_wrap::ParamFrame f(fn);
        f.SetRaw(L"inText", ftext, sizeof(ftext));
        f.Set<uint8_t>(L"type", type);
        f.Set<bool>(L"debug", false);
        f.Set<void*>(L"__WorldContext", gm);
        const bool ok = f.valid() && ue_wrap::Call(lib, f);
        UE_LOGI("info-hint: %ls (ok=%d)", text.c_str(), ok ? 1 : 0);
    });
}

// The peer that shows the tips, then captures.
void Show(const std::string& who, const std::wstring& label) {
    for (uint8_t type = 0; type < 4; ++type) { Hint(label, type); ::Sleep(250); }
    Mark(who + "_shown");
    ::Sleep(500);
    harness::screenshot::Capture((L"infohint-" + label + L"-own").c_str());
    if (!Await(who + "_captured")) return;
    ::Sleep(6000);   // the tips fade before the other peer shows its own
}

// The other peer: its window while those tips are up.
void Watch(const std::string& who, const std::wstring& label, const std::wstring& me) {
    if (!Await(who + "_shown")) return;
    ::Sleep(500);
    harness::screenshot::Capture((L"infohint-" + label + L"-on-" + me).c_str());
    Mark(who + "_captured");
}

}  // namespace

DWORD WINAPI InfoHintThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("info-hint: VERDICT ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    if (IsClientRole()) {
        bool ready = false;
        for (int i = 0; i < 480 && !ready; ++i) {
            OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
            if (!ready) ::Sleep(500);
        }
        ::Sleep(25000);   // the game's own intro tips clear first
        Mark("client_ready");
        Watch("host", L"host", L"client");
        ::Sleep(7000);
        Show("client", L"client");
    } else {
        if (!Await("client_ready")) return 0;
        Show("host", L"host");
        Watch("client", L"client", L"host");
    }
    ::Sleep(1000);
    UE_LOGI("info-hint: DONE");
    return 0;
}

}  // namespace harness::autotest
