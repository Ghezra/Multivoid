// coop/dev/hostmove_probe.h -- [dev] a client spawns a prop through the spawn menu and the host carries it off
// (VOTVCOOP_HOSTMOVE_PROBE=1 on every peer), the prop looked for on every peer, for the report "objects disappear for
// a client after the host moves an object the client placed".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::hostmove_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::hostmove_probe
