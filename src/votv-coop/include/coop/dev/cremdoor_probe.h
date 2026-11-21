// coop/dev/cremdoor_probe.h -- [dev] the cremator's door opened and shut by a client far from it, for the frame rate.
//
// VOTVCOOP_CREMDOOR_PROBE=1, with VOTVCOOP_PERF_PROBE=1. Slot 1, from 60 s after its session connects, where it
// joined: the nearest cremator's door opened and shut by the door's own verbs (prop_swinger open and close, as the
// cremator probe does), each 8 s, three times each (`[CREMDOOR]` with the distance), then nothing until 140 s. The
// `[perf] fps=` lines of every peer from 30 s to 60 s are the baseline, those from 60 s to 110 s the door's.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::cremdoor_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::cremdoor_probe
