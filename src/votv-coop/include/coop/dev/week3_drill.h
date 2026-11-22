// coop/dev/week3_drill.h -- [dev] the story's third week, played on three peers (VOTVCOOP_WEEK3_DRILL=<arm> on every
// peer; red arms in week3_red). Times are from each peer's own connect.
//   lantern  HOST, 30 s: the lantern prank fired (arirBuster). SLOT 1, 50 s: its player to a spot beside the prank's
//            notepad that holds it; 3 s after, its use of it. Every peer, each 5 s to 120 s (`[WEEK3] lantern`): the
//            notepads, and the Ariral it sends.
//   tower    HOST, 30 s: the event walk emptied and the clock set to day 22, 22:00. SLOT 1, 40 s: its player to a
//            spot in the radio tower ship's box that holds it; 150 s: its own copy's appear(true), as its own roll
//            makes it.
//            Every peer, each 5 s to 240 s (`[WEEK3] tower`): whether the ship shows.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::week3_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::week3_drill
