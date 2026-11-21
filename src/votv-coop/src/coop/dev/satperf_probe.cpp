// coop/dev/satperf_probe.cpp -- [dev] the SAT console lane's tick cost. See the header.

#include "coop/dev/satperf_probe.h"

#include "coop/config/config.h"
#include "ue_wrap/core/log.h"

#include <windows.h>

namespace coop::dev::satperf_probe {
namespace {

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_SATPERF") == "1";
    return v;
}

int64_t Freq() {
    static const int64_t f = [] {
        LARGE_INTEGER q;
        ::QueryPerformanceFrequency(&q);
        return q.QuadPart;
    }();
    return f;
}

uint64_t g_calls = 0, g_totalNs = 0, g_worstNs = 0, g_allCalls = 0, g_allNs = 0;
uint64_t g_nextLogMs = 0;

}  // namespace

uint64_t Start() {
    if (!On()) return 0;
    LARGE_INTEGER t;
    ::QueryPerformanceCounter(&t);
    return static_cast<uint64_t>(t.QuadPart);
}

void Stop(uint64_t start) {
    if (!start) return;
    LARGE_INTEGER t;
    ::QueryPerformanceCounter(&t);
    const uint64_t ns = (static_cast<uint64_t>(t.QuadPart) - start) * 1000000000ull / static_cast<uint64_t>(Freq());
    ++g_calls;
    g_totalNs += ns;
    if (ns > g_worstNs) g_worstNs = ns;
    const uint64_t now = ::GetTickCount64();
    if (!g_nextLogMs) g_nextLogMs = now + 10000;
    if (now < g_nextLogMs) return;
    g_nextLogMs = now + 10000;
    g_allCalls += g_calls;
    g_allNs += g_totalNs;
    UE_LOGI("[SATPERF] 10 s: %llu tick(s), %.3f ms total, %.2f us mean, %.1f us worst | session: %llu tick(s), "
            "%.2f us mean", static_cast<unsigned long long>(g_calls), g_totalNs / 1e6,
            g_calls ? g_totalNs / 1e3 / g_calls : 0.0, g_worstNs / 1e3, static_cast<unsigned long long>(g_allCalls),
            g_allCalls ? g_allNs / 1e3 / g_allCalls : 0.0);
    g_calls = g_totalNs = g_worstNs = 0;
}

}  // namespace coop::dev::satperf_probe
