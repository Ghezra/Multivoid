// coop/dev/satpanels_drill.h -- [dev] each SAT console its own terminal, every player's (VOTVCOOP_SATPANELS_DRILL=1,
// three peers). Every peer picks the same two consoles, A and B: the first two of the desk's panels whose dishes
// have different servers.
//   HOST    -- once slot 2's world is ready: puts a disc into B's server if its slot is empty. At +30 s shows B and
//              must read slot 2's sv.hash there and no sd.cal; at +35 s raises serverBroken on its own terminal for
//              A's server, and at +40 s must read the alert on the ROOT console and not on B; at +45 s B's server
//              must still hold its disc.
//   SLOT 1  -- 10 s after slot 2's puppet is here: at A, sv.eject must answer A's empty slot; then sd.cal, which
//              holds A busy for half a minute.
//   SLOT 2  -- 15 s after its world is ready: at B, sv.hash must answer, not "The terminal is busy"; 8 s later
//              at A, it must read A's sd.cal and its precision lines.
// Lines `[SATPANELS] <peer> <check> PASS|FAIL`, and `[SATPANELS] host DONE`.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::satpanels_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::satpanels_drill
