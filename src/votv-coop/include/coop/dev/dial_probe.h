// coop/dev/dial_probe.h -- [dev] the signal desk's frequency dial turned by a client, its offset logged on both peers
// (VOTVCOOP_DIAL_PROBE=1), for Ghezra/Multivoid issue 32: the client switches the frequency filter on, sets the dial's
// speed as a scroll does and sets it back to 0, and both log the desk's frequency offset fifty times a second.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::dial_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::dial_probe
