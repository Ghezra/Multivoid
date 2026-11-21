// coop/dev/satperf_probe.h -- [dev] what the SAT console lane's tick costs (VOTVCOOP_SATPERF=1).
//
// The session's desk tick wraps sat_console_sync::Tick in Start and Stop; every 10 s each peer logs the calls,
// their total, mean and worst time, and the session's running totals (`[SATPERF]`). Off, Start answers 0 and Stop
// does nothing.

#pragma once

#include <cstdint>

namespace coop::dev::satperf_probe {

// A start stamp, or 0 when the probe is off.
uint64_t Start();
// The time since `start` (no-op for 0), and every 10 s the window's line.
void Stop(uint64_t start);

}  // namespace coop::dev::satperf_probe
