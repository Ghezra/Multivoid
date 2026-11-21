// coop/dev/beer_probe.h -- [dev] a client takes the beer's nausea, and every peer watches the drinker.
//
// VOTVCOOP_BEER_PROBE=1. Slot 1, 30 s after its session connects: the gamemode's addEffect("nausea") with the strength and
// time of VOTVCOOP_BEER_PROBE_N beers (default 4; one beer is strength 2 for 10 s, each more adding to both). Every
// peer, each 3 s from 25 s to 150 s: its own player's ragdoll state and nausea, and each puppet's ragdoll display and
// body meshes' visibility (`[BEER]`).

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::beer_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::beer_probe
