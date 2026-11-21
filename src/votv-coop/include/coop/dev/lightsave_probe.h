// coop/dev/lightsave_probe.h -- [dev] lights a client switched on, through the host's save and its next load.
//
// VOTVCOOP_LIGHTSAVE_PROBE=flip: slot 1, 40 s after its session connects, presses (use(), as a player's press does)
// each light switch whose group is off and whose breaker is on, in key order, until one group is on (five at most);
// the host, at 75 s, runs the game's own save (mainGamemode_C::save, not a quick save). =look: nothing is pressed.
// Either way, every peer at 30 s, 70 s and 110 s logs each switch's group as it holds it (`[LIGHTSAVE]`): run flip,
// then look on the save it wrote, and a group the first run left on that the second loads off is the card's bug.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::lightsave_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::lightsave_probe
