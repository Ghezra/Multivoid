// coop/dev/satlog_probe.cpp -- [dev] each peer's SAT console screen, logged. See the header.

#include "coop/dev/satlog_probe.h"

#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/desk/sat_console.h"

#include <windows.h>

#include <cstdint>
#include <string>

namespace coop::dev::satlog_probe {
namespace {

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_SATLOG_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
size_t g_lastLen = static_cast<size_t>(-1);

}  // namespace

void Tick() {
    if (!On()) return;
    if (!g_startAt) g_startAt = Now();
    const uint64_t t = Now() - g_startAt;
    if (t < 20000u || t > 180000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    if (!ue_wrap::sat_console::EnsureResolved()) return;
    void* const term = ue_wrap::sat_console::LocalTerminal();
    std::wstring log;
    const bool ok = term && ue_wrap::sat_console::ReadLog(term, log);
    if (ok && log.size() == g_lastLen) return;  // unchanged: said once
    g_lastLen = ok ? log.size() : static_cast<size_t>(-1);
    std::wstring tail = log.size() > 120 ? log.substr(log.size() - 120) : log;
    for (wchar_t& c : tail)
        if (c == L'\n' || c == L'\r') c = L'|';
    UE_LOGI("[SATLOG] t=%llus slot=%d terminal=%d len=%zu tail=\"%ls\"", static_cast<unsigned long long>(t / 1000),
            coop::players::Registry::Get().LocalPeerId(), term ? 1 : 0, log.size(), tail.c_str());
}

}  // namespace coop::dev::satlog_probe
