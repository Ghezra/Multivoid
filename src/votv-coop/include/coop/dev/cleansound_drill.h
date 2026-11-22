// coop/dev/cleansound_drill.h -- [dev] the cleaning sounds on the sound relay, on three peers (VOTVCOOP_CLEANSOUND_DRILL=1
// on every peer). Times are from each peer's own connect.
//   THE HOST, the actor (a client's own jump is not a move the host accepts, and the host's order lands a body in the
//   clip check on this save): 40 s, a sponge spawned and taken into its hand. 46 s: the save's water bucket moved a metre
//   ahead of it on its floor, its view onto it. 48 s: the sponge's own use (playerHandUse_LMB), whose trace into the bucket dips it (its 2D
//   wade). 53 s: five strokes, 0.5 s apart, of the decal nearest the bucket (grime_C::clean with the sponge, as a stroke
//   calls it: its squelch).
//   Every peer, 75 s (`[CSND] done`): the relay's counts, sent here, played from a peer and refused.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::cleansound_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::cleansound_drill
