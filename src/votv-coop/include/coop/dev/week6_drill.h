// coop/dev/week6_drill.h -- [dev] the story's sixth week, played on three peers (VOTVCOOP_WEEK6_DRILL=<arm> on every
// peer; red arm in week6_red). Times are from each peer's own connect.
//   borg     HOST, 30 s: the Rozital mothership's row fired (borgRozital).
//   morning  HOST, 30 s: the peeping UFO's row fired (morningGay); 40 s: its own player into the UFO's box; 46 s: its
//            player 60 m off. SLOT 1, 46 s: its player where the host's stood, so the UFO looks at it.
//   dropcar, dropbody  HOST, 30 s: the drop saucer's row fired (fallcar_0, fallbody_0); 40 s: its forceDeploy.
//   hole     HOST, 30 s: the Hole Repair's row fired (rozitalHole); 40 s: into its controller's box; 75 s: slot 1
//            ordered 70 m from the device, among the guards (VOTVCOOP_WEEK6_HOLE_OUT=host: the host goes); 190 s: by
//            the Igetis.
// Every peer, each 3 s to 150 s, logs the arm's actors on this machine, shown or hidden, and their state
// (the mothership's alpha and ships, the UFO's fly and flyAway and the slot it looks at, the saucer's lowered body, its
// light weight and the props of its drop class).

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::week6_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::week6_drill
