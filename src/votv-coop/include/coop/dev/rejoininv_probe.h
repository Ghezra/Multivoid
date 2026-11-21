// coop/dev/rejoininv_probe.h -- [dev] every peer's own inventory logged across a client's leave and rejoin
// (VOTVCOOP_REJOININV_PROBE=1 on every peer), the host holding two items of its own, for the report "players who
// rejoin the server receive the host's items".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::rejoininv_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::rejoininv_probe
