// coop/world/hole_sync.h -- the Hole Repair (rozitalHole) opens on every player's machine.
//
// The event spawns event_bottomHoleController_C on the host alone, a save actor whose box (by the main computer) opens
// the Hole once a player stands in it: the bottom room (holeBottom_C), the device and its four beacons (props, on the
// prop lanes), the patrol controller (its guards, kavotia_C, on the NPC lane, and its igetis_C, on the world-actor
// lane, both caught as its spawns and inert on a client: coop/world/spawn_authority), the grate's and the fence's
// props. A joiner loads the controller and the room from the save it joined with.
// HOST: while its room stands, its place is sent every 5 s (EventCue 117), and while its controller stands, that it
// does (118). CLIENT: the controller's box is refused, so a joiner's own copy never opens a second Hole; a client with
// no room there spawns one at 117, and its own controller ends once the host's 118 stops. Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct EventCuePayload;
}  // namespace coop::net

namespace coop::hole_sync {

// The EventCue id; the cue registry's ids are append-only.
inline constexpr uint32_t kCueHoleRoom = 117;   // the bottom room's place
inline constexpr uint32_t kCueHoleLive = 118;   // the host's controller stands

void Install(coop::net::Session* session);

// The watch, retried until registered; the host's room heartbeat. Game thread, once a tick.
void Tick();

// A client's receiver: true when the cue was this lane's.
bool OnCue(const coop::net::EventCuePayload& p);

void OnDisconnect();

}  // namespace coop::hole_sync
