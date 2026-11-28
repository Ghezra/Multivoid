// coop/world/agrav_sync.h -- the Ariral's float (trigger_agrav_C): the props it lifts float on every peer.
//
// The event runs on the host alone. Five seconds in, the trigger gathers the physics bodies within 100 m of the host's
// camera into `comps`, and for each in turn switches gravity off and sets it rising at 10 cm/s with a spin; after its
// time it switches each back on. The HOST watches each simulating body in a live trigger's `comps`: one whose gravity
// went off and that then rises 3 cm is an AgravState `lifted` row with the speed it rose at, and one whose gravity comes
// back on a `dropped` row, by host element id. A CLIENT switches its copy's gravity the same way, sets it rising at the
// host's speed (again once a second when it stopped and no drive row holds it), and once a dropped copy rests asks the
// host where its prop rests (coop/props/prop_rest_sync).
//
// The float's show -- its shimmer, a light at the player's camera, the hum -- is on the trigger, which a client lacks.
// The HOST sends the trigger's cloak(true) as EventCue 135 at the trigger, cloak(false) as 136, and 135 to a joiner
// while it floats; a CLIENT spawns its own copy of the trigger there, never run, and shows and ends the show on it.
// Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct AgravStatePayload;
struct EventCuePayload;
}  // namespace coop::net

namespace coop::agrav_sync {

void Install(coop::net::Session* session);

// HOST: the scan of the live triggers' bodies, a few times a second while one holds any. A client's call is a no-op.
void Tick();

// CLIENT: the host's switches, applied to this client's copies.
void OnState(const coop::net::AgravStatePayload& p);

// The EventCue ids; the cue registry's ids are append-only.
inline constexpr uint32_t kCueFloatBegin = 135;   // the float's show begins at x, y, z
inline constexpr uint32_t kCueFloatEnd = 136;     // the float's show ends

// A client's receiver: true when the cue was this lane's.
bool OnCue(const coop::net::EventCuePayload& p);

// HOST: slot `slot`'s connect replay: the float's begin, while it floats.
void SendJoinSnapshotForSlot(int slot);

// The session ended: what was told is forgotten.
void OnDisconnect();

}  // namespace coop::agrav_sync
