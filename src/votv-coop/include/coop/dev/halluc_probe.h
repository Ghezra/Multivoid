// coop/dev/halluc_probe.h -- [dev] the hallucinations are their player's own (VOTVCOOP_HALLUC_PROBE=1, three peers,
// with VOTVCOOP_FILL_NEEDS=0).
//   HOST, 40 s after its session connects: food 0 for five addHallFood calls, then 100; sleep held at 3 (the
//       spawner weighs 20 - sleep; at 0 the player faints, and a sleeper is skipped), so its own spawner rolls.
//   SLOT 1, 60 s after its session connects: food held at 5, so its roaches can look like burgers; sleep at 3.
//   SLOT 2: nothing, the control.
// Every peer, each 5 s to 200 s (`[HALLUC]`): its Insomniacs and the nearest one's distance to its own player, its
// hallucinated burgers and how many an element binds, and how many of its roaches show the burger mesh. At 180 s the
// needs go back to 100 and every peer's own Insomniacs are destroyed.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::halluc_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::halluc_probe
