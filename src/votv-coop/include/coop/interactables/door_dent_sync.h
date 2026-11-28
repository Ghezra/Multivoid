// coop/interactables/door_dent_sync.h -- a base door's leaves stand where the host's hit pushed them.
//
// door_C::addDamage pushes each leaf part of the way open, by the damage, and opens the door only once they have gone
// far enough; a client's copy ran no hit (the door verb lane runs it on the host), so a door an Ariral punched, or a
// player hit, stood shut there while the host's stood ajar (Ghezra/Multivoid issue 63). Now, on DoorDentState:
//   HOST   -- after every addDamage on a door the door lane indexes, sends its two leaves' relative locations when they
//             moved; to a joiner, every door it sent, as the door stands now.
//   CLIENT -- writes both leaves where the host's stand, unless its copy is swinging: the open state's own swing, from
//             DoorState, ends where the host's does. Game thread.

#pragma once

namespace coop::net {
class Session;
struct DoorDentStatePayload;
}  // namespace coop::net

namespace coop::door_dent_sync {

void Install(coop::net::Session* session);
void Tick();

void OnState(const coop::net::DoorDentStatePayload& p);  // CLIENT

// HOST: slot `slot`'s connect replay: every door sent, to it alone.
void SendJoinSnapshotForSlot(int slot);

void OnDisconnect();

}  // namespace coop::door_dent_sync
