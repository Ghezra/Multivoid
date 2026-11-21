// coop/dev/deskneedle_probe.h -- [dev] the signal desk's detection needle and its save latch, on every peer.
//
// VOTVCOOP_DESKNEEDLE_PROBE=1, run with desk_verb_drill. Every peer, each second from 20 s to 240 s after its session
// connects, when either moved: the desk's DL_resDetecPercent (the needle; the SAVE button saves only at 1.0) and canDL
// (`[NEEDLE]`), so a SAVE press that saved nothing is a needle below 1.0 at that second, or not.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::deskneedle_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::deskneedle_probe
