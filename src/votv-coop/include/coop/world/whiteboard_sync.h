// coop/world/whiteboard_sync.h -- a whiteboard's picture is the world's, on every peer.
//
// A stroke on a board ends with prop_whiteboard_C::Release, and the game serializes the finished board through
// whiteboard_C::TextureToString (a base64 picture) and puts one back through stringToTexture. This lane carries that
// picture: the drawing peer sends it to the host, the host applies it to its own board and sends it to every other peer,
// and a joiner is sent each board's picture at its world-ready. Erasing and a full clear are strokes too, so they cross
// the same way. One picture per finished stroke, coalesced; a large board that does not fit the wire is refused once and
// named in the log. Game thread.

#pragma once

#include <cstdint>
#include <string>

namespace coop::net {
class Session;
struct BlobChunkPayload;
}

namespace coop::whiteboard_sync {

// Cache the session and seed the board list.
void Install(coop::net::Session* session);

// Per gameplay tick: the board list, the stroke trigger's dirty marks, and the sends.
void Tick();

// A board picture's chunks arrived (all of them). `senderSlot` is the peer that sent it.
void OnChunk(const coop::net::BlobChunkPayload& chunk, int senderSlot);

// HOST: slot `slot`'s world is up: every board's picture, to it alone.
void OnPeerWorldReady(int slot);

void OnDisconnect();

enum class Count : uint8_t { Sent, Applied, Refused, JoinSent, kCount };

}  // namespace coop::whiteboard_sync