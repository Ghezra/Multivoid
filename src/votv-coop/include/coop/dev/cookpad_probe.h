// coop/dev/cookpad_probe.h -- [dev] a food a client sets down on a cooking pad, on both peers
// (VOTVCOOP_COOKPAD_PROBE=1): the host places a powered pad and a raw steak before the client, the client takes the steak
// into its hand, sets it on the pad as a player does and lets it go, and both log where each copy rests and its cooking.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::cookpad_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::cookpad_probe
