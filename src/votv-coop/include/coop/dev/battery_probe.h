// coop/dev/battery_probe.h -- [dev] the battery charger, rechargeable batteries and the car battery charger on the rig
// (VOTVCOOP_BATTERY_PROBE=1 on every peer; the steps are in the .cpp).
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::battery_probe {

// The dev lanes' tick. A latched read when the probe is off. Game thread.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::battery_probe
