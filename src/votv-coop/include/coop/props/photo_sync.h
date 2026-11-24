// coop/props/photo_sync.h -- a printed photo shows the same picture on every peer; a client's print is the host's.
//
// The printer camera (prop_digcam_print_C; the digital camera, prop_digcam_C, only captures) prints the gallery's last
// capture into the save object's `photos_phys` at the same index and pockets a prop_photo_C named by it, whose loadImage
// shows photos_phys[index] (ue_wrap/actors/photo_store). The index was each save's own, so a photo crossing peers showed
// the receiver's picture of it. Now the host's save owns every index, on the PhotoImage kind (chunked, in parts):
//   HOST   -- every picture its copies load while a client is connected goes to every client (op 2) with its index. A
//             client's print (op 1) is judged (its camera within 6 m, one a second), appended to the host's gallery so
//             no host print takes its index, written to photos_phys and sent to every client, naming the printer.
//   CLIENT -- its makePhoto is refused and run here 0.25 s on: its own paper check, then its capture asked of the host;
//             with no paper the game's own makePhoto runs, for its hint. A picture from the host is written at its
//             index and every copy of it reloads; the printer spawns its photo there and pockets it.
// A joiner's save carries no pictures: the connect replay sends every printed one. A client's gallery keeps its own
// captures; the host's laptop shows a client's print after the next load; the flash is the taker's. Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct BlobChunkPayload;
}  // namespace coop::net

namespace coop::photo_sync {

void Install(coop::net::Session* session);

// Settles the watches, runs a client's due prints and sweeps the half-assembled pictures.
void Tick();

// One chunk of a PhotoImage message (game thread, in the drain).
void OnChunk(coop::net::Session& session, const coop::net::BlobChunkPayload& chunk, int senderPeerSlot);

// HOST: the slot's connect replay: every printed picture in the host's save, to that slot alone.
void SendJoinSnapshotForSlot(int slot);

// HOST: the index the last client print was filed at, or -1 (the drill reads it).
int32_t LastClientPrint();

// A peer left: the parts it had half sent go.
void OnPeerGone(uint8_t slot);

void OnDisconnect();

}  // namespace coop::photo_sync
