// coop/dev/radar_probe.h -- [dev] the radar's dots on every peer. See the source.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::radar_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::radar_probe
