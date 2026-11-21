// coop/dev/carry_probe.h -- [dev] a player picks up a prop of a named class, carries it and lets it go, every peer
// logging the prop (VOTVCOOP_CARRY_PROBE=1), for the Trello card "microwave physics desync".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::carry_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::carry_probe
