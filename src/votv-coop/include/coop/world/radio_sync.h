// coop/world/radio_sync.h -- one track, position and play state for a placed internet radio on every peer.
//
// Each peer's radio plays through its own media player, picks its own next track and starts when its own player presses,
// so the host's is the state: it sends what it reads, and a client's copy is moved to it. A client whose player has the
// radio's interface open sends what its player changed. A track crosses as a hash, and a peer without it in its own
// catalog, or with the Custom Content rule off, stays silent. The radio's sound stays each peer's own. The shape follows Watchers of the Void's radio lane.

#pragma once

namespace coop::net {
class Session;
struct RadioStatePayload;
}  // namespace coop::net

namespace coop::radio_sync {

void Install(coop::net::Session* session);

// Per gameplay tick: the poll, four times a second. Game thread.
void Tick();

// The host's state of a radio, or on the host a client's edit. Validated at the router.
void OnState(int senderSlot, const coop::net::RadioStatePayload& p);

// HOST: a joiner reached its ready edge. Every radio goes out again.
void QueueConnectBroadcastForSlot(int slot);

void OnDisconnect();

}  // namespace coop::radio_sync
