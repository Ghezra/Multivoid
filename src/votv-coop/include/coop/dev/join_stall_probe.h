// coop/dev/join_stall_probe.h -- [dev] a client's game thread held still while it joins (VOTVCOOP_JOIN_STALL_MS on the
// client): a player's join ended in "No answer from the host" after a 22.7 s frame.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::join_stall_probe {

// Per pump tick. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::join_stall_probe
