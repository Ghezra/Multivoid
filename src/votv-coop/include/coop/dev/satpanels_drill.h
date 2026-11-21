// coop/dev/satpanels_drill.h -- [dev] each SAT console its own terminal, every player's (VOTVCOOP_SATPANELS_DRILL,
// three peers). Every peer picks the same consoles A and B, the first two panels whose dishes have different
// servers, and enters one as its panel's use does (actionOptionIndex 4 on its box).
//   =1: slot 1 at A: sv.eject answers A's empty slot, then sd.calall. Slot 2: refused A, holds B, where sd.calch
//       answers while A runs and sd.calall is refused with A's name. The host puts a disc into B's server, reads each
//       console's own lines, raises serverBroken and reads it on ROOT and not on B, and B's server keeps its disc.
//   =ragdoll: slot 1 ragdolls out of A just after its sd.calall; slot 2 enters A, reads the run and is told it is
//       busy, is refused sd.calall at B while A runs, and runs it once A has finished.
//   =timeout, with VOTVCOOP_SAT_LOCK_TIMEOUT_S: slot 2's sd.calall at B runs once A's has outlasted the bound.
//   =reconnect: slot 1's sd.calall at A over five zeroed dishes outlives its kill; slot 2 is refused sd.calall
//       at B, and slot 1's second life (VOTVCOOP_SATPANELS_LIFE=2) finds A busy with the run and sees it end.
// Each check logs its verdict under [SATPANELS], and the host its end.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::satpanels_drill {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::satpanels_drill
