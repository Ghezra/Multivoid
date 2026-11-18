// coop/dev/hunger_sleep_probe.h -- [dev] a hungry player tries to sleep (VOTVCOOP_HUNGER_SLEEP_PROBE=1 on every peer),
// for Ghezra/Multivoid#24: a client did not always get the "too hungry to sleep" hint.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::hunger_sleep_probe {

// The dev lanes' tick. Game thread.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::hunger_sleep_probe
