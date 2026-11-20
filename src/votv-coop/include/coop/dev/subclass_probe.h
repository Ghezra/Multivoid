// coop/dev/subclass_probe.h -- [dev] the exact-class sweep's fixes on the rig (VOTVCOOP_SUBCLASS_PROBE=1 on every peer):
// a client's conversion of a raw patty, a client-side blue brain cheese's tick, a client's shot from the rapid RPG.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::subclass_probe {

// The dev lanes' tick. A latched read when the probe is off. Game thread.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::subclass_probe
