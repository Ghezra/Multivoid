// coop/dev/slip_probe.h -- [dev] a player walks onto a soap and slips (VOTVCOOP_SLIP_PROBE=host|client names the
// slipper), its health, ragdoll and death logged on every peer, for the Trello card "banana and soap slip kills".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::slip_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::slip_probe
