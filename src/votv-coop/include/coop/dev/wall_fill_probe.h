// coop/dev/wall_fill_probe.h -- [dev] an unfinished wall filled with bricks by both players (VOTVCOOP_WALL_FILL_PROBE=1):
// the host places it and drops bricks in, a client grabs a brick and carries it in, and both log the wall's count.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::wall_fill_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::wall_fill_probe
