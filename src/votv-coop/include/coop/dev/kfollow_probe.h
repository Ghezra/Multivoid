// coop/dev/kfollow_probe.h -- [dev] an Omega following a player (VOTVCOOP_KFOLLOW_PROBE=client|host, the follower, on
// both peers): the follower tells it to follow and walks to and fro; both peers log each frame the Omega leaps.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::kfollow_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::kfollow_probe
