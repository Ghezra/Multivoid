// coop/interactables/window_dirt_sync.h -- the bay window's grime is the host's picture on every peer.
//
// The bay window (d_window_C) holds its grime in a render target. Its own timer (`dirtify`, every 120 s) draws a dirt
// splotch at a random place on each peer, so every peer's window got its own dirt to clean (issue 51); the dabs that
// clean it cross on their own lane (coop/interactables/window_stroke_sync), but a dab that lane cannot send (a thrown
// sponge's) and every splotch left the pictures apart. Now, on the WindowImage kind:
//   HOST   -- its timer's splotch runs; its window's whole picture (the game's own getCode, as its save holds it) is
//             sent to every client after a splotch, after the dabs on it have stopped for 1.5 s, and to a joiner.
//   CLIENT -- its timer's splotch is refused. The host's picture replaces its window's, drawn inside the window's own
//             canvas session (ue_wrap/devices/window_canvas), once no dab of its own player's is in flight.
// Game thread.

#pragma once

namespace coop::net {
class Session;
struct BlobChunkPayload;
}  // namespace coop::net

namespace coop::window_dirt_sync {

void Install(coop::net::Session* session);
void Tick();

// CLIENT: a part of the host's picture (chunked).
void OnChunk(const coop::net::BlobChunkPayload& chunk, int senderPeerSlot);

// HOST: slot `slot`'s connect replay: the window's picture, to it alone.
void SendJoinSnapshotForSlot(int slot);

void OnDisconnect();

}  // namespace coop::window_dirt_sync
