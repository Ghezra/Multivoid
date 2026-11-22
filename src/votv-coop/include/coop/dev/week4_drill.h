// coop/dev/week4_drill.h -- [dev] the story's fourth week, played on three peers (VOTVCOOP_WEEK4_DRILL=<arm> on every
// peer; red arms in week4_red). Times are from each peer's own connect.
//   obelisk  HOST, 30 s: the warning obelisk forced through its box. Every peer, each 2 s to 120 s (`[WEEK4] obelisk`):
//            its obelisks, whether each has fallen and its mesh's height, the rocks, the standing obelisk, its events.
//   gun      HOST, 30 s: the dropped gun's row fired (toeStab), which arms the garage box on every peer. SLOT 1, 45 s:
//            its player into its own copy of the box (the box's overlap handler, with its own
//            pawn). Every peer, each 5 s to 120 s (`[WEEK4] gun`): its guns.
//   box      HOST, 30 s: the row of VOTVCOOP_WEEK4_BOX=<row>|<box name> fired, arming its box. SLOT 1, 45 s: its player
//            to the box's middle. Every peer, each 5 s to 120 s (`[WEEK4] box`): the box's count, N.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::week4_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::week4_drill
