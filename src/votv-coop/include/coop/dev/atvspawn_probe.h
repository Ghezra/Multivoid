// coop/dev/atvspawn_probe.h -- [dev] an ATV spawned through the spawn menu by a client or the host
// (VOTVCOOP_ATVSPAWN_PROBE=1 on every peer), each peer's ATVs and their visibility logged, for the report "an ATV
// spawned in invisible to one player while it stayed fully functional for the other".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::atvspawn_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::atvspawn_probe
