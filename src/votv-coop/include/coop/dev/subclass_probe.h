// coop/dev/subclass_probe.h -- [dev] the exact-class sweep's fixes on the rig (VOTVCOOP_SUBCLASS_PROBE=<mode> on every peer;
// the modes are in the .cpp).
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::subclass_probe {

// The dev lanes' tick. A latched read when the probe is off. Game thread.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::subclass_probe
