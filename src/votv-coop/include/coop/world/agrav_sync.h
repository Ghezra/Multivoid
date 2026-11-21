// coop/world/agrav_sync.h -- the Ariral's float (trigger_agrav_C): the props it lifts float on every peer.
//
// The event runs on the host alone (event_fire_policy: its pick is random and its props are the host's). Five seconds
// into it the trigger gathers the physics bodies within 100 m of the host's camera into its `buf`, shuffles them, and for
// each turns gravity off and sets it rising at 10 cm/s with a random spin; after its time it turns each back on, one a
// tick. A client ran none of it, so only the host saw the room float. The HOST now watches the gravity of each body in a
// live trigger's `buf` and of each it has told: a switch off is an AgravState `lifted` row, a switch back on a `dropped`
// one, by host element id. A CLIENT switches its own copy's gravity the same way, lifts it at the event's own speed, and
// once a dropped copy rests asks the host where its prop rests (coop/props/prop_rest_sync), so every copy comes down
// where the host's did. Game thread.

#pragma once

namespace coop::net {
class Session;
struct AgravStatePayload;
}  // namespace coop::net

namespace coop::agrav_sync {

void Install(coop::net::Session* session);

// HOST: the scan of the live triggers' bodies, a few times a second while one holds any. A client's call is a no-op.
void Tick();

// CLIENT: the host's switches, applied to this client's copies.
void OnState(const coop::net::AgravStatePayload& p);

// The session ended: what was told is forgotten.
void OnDisconnect();

}  // namespace coop::agrav_sync
