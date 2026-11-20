// coop/dev/issue42_probe.h -- [dev] Ghezra/Multivoid issue 42's session logs staged on the rig
// (VOTVCOOP_ISSUE42_PROBE=<mode> on every peer; the modes are in the .cpp).
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::issue42_probe {

// The dev lanes' tick. A latched read when the probe is off. Game thread.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::issue42_probe
