// coop/dev/binput_probe.h -- [dev] a client's puts from its inventory into a garbage bin, one after another
// (VOTVCOOP_BINPUT_PROBE=1, VOTVCOOP_BINPUT_ITEM=clump|disc): the host places a green garbage bin and three items before
// the client, the client takes the three into its inventory, opens the bin's screen and puts them in through the
// screen's em_take, the hotbar key's verb, then closes it and opens it again; both log the bin's records.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::binput_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::binput_probe
