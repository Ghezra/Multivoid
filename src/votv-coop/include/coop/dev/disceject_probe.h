// coop/dev/disceject_probe.h -- [dev] a disc put in a server box and ejected twice (VOTVCOOP_DISCEJECT_PROBE=1 on both
// peers), every peer counting the discs that carry its row.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::disceject_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::disceject_probe
