// coop/props/tape_sync.h -- a tape's repair is the host's.
//
// The office tape (prop_officetape_C), held, follows its holder's look to a prop (its tick sets `prop`), and its left
// click sets that prop's health (its comp_physicsImpact_C `health`) back to its full (`damageData.health`), takes one of
// its own `uses` and goes at none; on a prop at full it says the object is fine. A prop's health is the host's (a client
// never decides a prop's damage: coop/props/prop_damage_gate), so a client's repair mended only its own copy and the
// host's prop broke as before. Now, on the TapeAsk and TapeState kinds:
//   CLIENT -- its click on a prop the host names is refused and asked of the host (the tape and the prop).
//   HOST   -- judges it (the prop within 6 m of the sender's body, the tape a tape, four asks a second), mends its own
//             prop or finds it at full, takes the use from its copy of the tape, and answers the sender.
//   CLIENT -- the answer runs the tape's own click on its copy, the prop's copy set to what the host found, so its hint,
//             its sound, its uses and, at the last use, its own going (as a held bag's last unit goes) are the game's.
// A click with no prop in view runs as the game's. The host's own repairs run as the game's. Game thread.

#pragma once

namespace coop::net {
class Session;
struct TapeAskPayload;
struct TapeStatePayload;
}  // namespace coop::net

namespace coop::tape_sync {

void Install(coop::net::Session* session);
void Tick();

void OnAsk(int senderSlot, const coop::net::TapeAskPayload& p);  // HOST
void OnState(const coop::net::TapeStatePayload& p);              // CLIENT

void OnDisconnect();

}  // namespace coop::tape_sync
