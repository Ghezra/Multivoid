// coop/dev/reelspam_probe.h -- [dev] a player spams the tape recorder's eject (VOTVCOOP_REELSPAM_PROBE=host|client names
// the ejecter), every peer counting its reels, for the Trello card "log duplication".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::reelspam_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::reelspam_probe
