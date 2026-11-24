// coop/interactables/gasoline_sync.h -- the cremator's fuel and a gas can's fuel are the host's, on every peer.
//
// The cremator makes fuel from what it burns (`gasAmount`); its valve (`pouring`) pours it, every timer step, into a gas
// can (prop_gascan_C) under its nozzle, raising the can's `amount`. A client's valve press was refused and its copy held
// no fuel, and no lane carried a can's fuel, so a can the host's cremator filled stayed empty on a client (issue 50).
// Now, with the cremator intent's valve press (coop/interactables/cremator_intent, actions 2 and 3):
//   HOST   -- reads each cremator's fuel and valve and each can's fuel 5 times a second, and sends what changed
//             (CrematorFuel; GasCanState), every one to a joiner. It takes a client's report that its copy of a can
//             lost fuel (GasCanSpend: a pour into a generator, a woodchipper or a lamp) as the can's fuel, never higher.
//   CLIENT -- writes the host's values on its copies (the cremator's through its own updPour, which draws the stream);
//             its copy of a cremator pours nothing (pourOut refused). A copy of a can whose fuel fell below the host's
//             last value is reported; one that rose is put back. Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct CrematorFuelPayload;
struct GasCanPayload;
}  // namespace coop::net

namespace coop::gasoline_sync {

void Install(coop::net::Session* session);
void Tick();

// HOST: the cremator's valve or fuel changed by a lane's hand; it is sent at the next tick whatever the step.
void MarkCremator(void* cremator);

// HOST: slot `slot`'s connect replay: every cremator's fuel and every can's, to it alone.
void SendJoinSnapshotForSlot(int slot);

void OnCrematorFuel(const coop::net::CrematorFuelPayload& p);  // CLIENT
void OnCanState(const coop::net::GasCanPayload& p);            // CLIENT
void OnCanSpend(int senderSlot, const coop::net::GasCanPayload& p);  // HOST

void OnDisconnect();

}  // namespace coop::gasoline_sync
