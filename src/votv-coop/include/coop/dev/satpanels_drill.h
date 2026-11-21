// coop/dev/satpanels_drill.h -- [dev] each SAT console its own terminal, every player's (VOTVCOOP_SATPANELS_DRILL=1,
// three peers). Every peer picks the same two consoles, A and B: the first two of the desk's panels whose dishes
// have different servers. A player enters a console as its panel's use does (actionOptionIndex 4 on its box).
//   SLOT 1  -- 10 s after slot 2's puppet is here: enters A; sv.eject must answer A's empty slot; then sd.calall,
//              which holds A for a while; leaves A at +60 s.
//   SLOT 2  -- from 15 s after its world is ready: entering A must be refused (slot 1 uses it); entering B must
//              hold; at B, sd.calch must answer while A runs, and sd.calall must be refused with A's name.
//   HOST    -- from slot 2's world-ready: puts a disc into B's server if its slot is empty; at +35 s must read A's
//              sd.calall on A and slot 2's sd.calch on B and not the sd.calall there; at +40 s raises serverBroken
//              for A's server on its own terminal, and at +45 s must read it on the ROOT console and not on B; at
//              +50 s B's server must still hold its disc.
// Each check logs its verdict under [SATPANELS], and the host its end. Slot 2 enters B a second time when the
// first entry, just after its refusal at A, did not hold, and logs the retry's verdict apart.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::satpanels_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::satpanels_drill
