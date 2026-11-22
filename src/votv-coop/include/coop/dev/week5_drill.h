// coop/dev/week5_drill.h -- [dev] the story's fifth week, played on three peers (VOTVCOOP_WEEK5_DRILL=<arm> on every
// peer; red arm in week5_red). Times are from each peer's own connect.
//   scouts   HOST, 30 s: the Rozital scouts' row fired (tentacleBalls). SLOT 1, from 45 s: its player to a spot 15 m
//            from the first scout, kept when the player still stands near it 2 s later. Every peer, each 3 s to
//            240 s (`[WEEK5] scouts`): the scouts, hostile, chasing and pinning, the host's and slot 1's distance to
//            the nearest, and whether this peer's own player plays a body animation (a pin).
//   solto    HOST, 30 s: the garage Soltomia's row fired (soltoClean); 40 s: its own player into the Soltomia's box;
//            70 s: its opened run. Every peer, each 3 s to 150 s (`[WEEK5] solto`): its Soltomias, those shown, and the
//            Soltomia's sponges.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::week5_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::week5_drill
