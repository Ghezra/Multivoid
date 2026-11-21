// coop/dev/hallfood_probe.h -- [dev] a starving player's food hallucination, seen from every peer.
//
// VOTVCOOP_HALLFOOD_PROBE=1, with VOTVCOOP_FILL_NEEDS=0. The gamemode's own addHallFood (what the hunger ticker runs on
// a starving player: a prop_burgerHallucinate_C spawned on the floor near that player's camera) called five times on
// the host at 40 s after its session connects, and five times on slot 1 at 70 s, the caller's food set to 0 for them
// (the call runs only below 20, with the chance 1 - food/20) and back to 100 after. Every peer, each 5 s from 30 s to
// 150 s: its live prop_burgerHallucinate_C actors, how many of them the element registry binds, and where
// (`[HALLFOOD]`), so a hallucination that reaches another peer's world is a count on a peer that called nothing.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::hallfood_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::hallfood_probe
