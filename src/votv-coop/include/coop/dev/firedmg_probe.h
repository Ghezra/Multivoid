// coop/dev/firedmg_probe.h -- [dev] a client set on fire, its health and every other player's read through the burn
// (VOTVCOOP_FIREDMG_PROBE=1), for the Trello card "fire damage and visual desync".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::firedmg_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::firedmg_probe
