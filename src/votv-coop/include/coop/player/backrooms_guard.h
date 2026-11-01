// coop/player/backrooms_guard.h -- no trip to the backrooms while a session runs.
//
// mainPlayer_C's tick traces from last frame's camera to this one, and a hit on static geometry is the game's way into the
// backrooms: a black screen, a save, and a level travel, which ends the session for every peer. A peer's puppet runs that graph
// too, so a client that glitches through a wall can send the host. The whole branch stands behind mainGamemode_C.canBackrooms,
// which reads backroomsEnabled, and the gamemode turns that on three seconds into each world. This poll keeps it off, on both
// roles, so a player that clips stays in the world. Falling under the map ends in the game's own return to the spawn point.

#pragma once

namespace coop::net {
class Session;
}  // namespace coop::net

namespace coop::backrooms_guard {

void Install(coop::net::Session* session);

// The poll, on either role.
void Tick();

// The flag is back on, for the world this peer stays in.
void OnDisconnect();

}  // namespace coop::backrooms_guard
