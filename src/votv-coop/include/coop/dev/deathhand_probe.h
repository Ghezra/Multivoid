// coop/dev/deathhand_probe.h -- [dev] a player dies with an item in its hand and is revived (VOTVCOOP_DEATHHAND_PROBE=
// host|client names who dies), its hand logged before and after, for the Trello card "items held at death cannot be
// moved to inventory".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::deathhand_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::deathhand_probe
