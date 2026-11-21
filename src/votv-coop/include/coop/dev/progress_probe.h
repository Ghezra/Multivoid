// coop/dev/progress_probe.h -- [dev] the progress bar a timed tool shows, raised on a client, for the frame rate.
//
// VOTVCOOP_PROGRESS_PROBE=1. Slot 1, from 60 s to 90 s after its session connects: the game's ui_linearProgress_C (the
// widget the hacksaw, the toolbox and a desk plate's unbolt show) made and added to the viewport, its bar's SetPercent
// called every frame as the tools do, then removed. Run with VOTVCOOP_PERF_PROBE=1 and compare the `[perf] fps=` lines
// of the 30 s before with those of the 30 s it shows (`[PROGRESS]` marks the edges).

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::progress_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::progress_probe
