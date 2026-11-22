// coop/dev/week2_drill.h -- [dev] the story's second week, played on three peers (VOTVCOOP_WEEK2_DRILL=<arm> on every
// peer; red arms in week2_red). Times are from each peer's own connect.
//   shrimp  HOST, 30 s: the event walk emptied and the day set to 13 (the Arirals eat from day 11). 40 s: its own
//           player 300 m off; a shrimp beside slot 1's player, the eater's last spot set on it. 100 s: a second shrimp
//           150 m from every player, the last spot set on that. Every peer, each 5 s to 160 s (`[WEEK2] shrimp`): the
//           shrimps and the empty packs near each spot. Red: week2_red=shrimp_open.
//   ships   HOST, 25 s: an earlier picnic's arrows cleared; 40 s: the picnic fired; 200 s: its clearing
//           (destroyPicnic). Slot 2 joins between the two. Every peer, each 10 s to 260 s (`[WEEK2] ships`): its warp
//           arrows. Red: week2_red=no_ships.
//   knock   HOST, 30 s: its own player 300 m off; 40 s: the knocker fired. SLOT 1, 60 s: its player into the
//           knocker's box. Every peer, each 2 s to 120 s (`[WEEK2] knock`): whether a knocker stands here.
//   rep     HOST, 40 s: +7 Ariral reputation through lib_C.addAriralRep. SLOT 1, 50 s: +100 of its own; 60 s: its
//           wake's +5; 70 s and 75 s: a photo of the visit ship, +15, twice. Every peer, each 5 s to 100 s (`[WEEK2]
//           rep`): its reputation. Red: week2_red=rep_open.
// The inbox count of the Ariral ship's mails is coop/dev/event_probe's (VOTVCOOP_EVENT_PROBE_EMAILS).

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::week2_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::week2_drill
