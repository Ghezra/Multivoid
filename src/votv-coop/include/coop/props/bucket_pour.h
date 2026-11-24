// coop/props/bucket_pour.h -- a tipped bucket's pour is the host's: its splash, its cleaning and its water.
//
// A water bucket (prop_bucket_C, the mop bucket under it) tipped while its mesh simulates, held in a player's grab, pours:
// 0.1 s after its tick lowers `height` it spawns one prop_sponge_bucketpour_C per 2 of the drop at its `pour` point, with
// the bucket's velocity and a scatter, whatever the water holds (the tipped pour reads no soap or brew). A blob that lands
// cleans the grime around it (cleanSponge), puts out fires and shorts a screen. A client's blobs were births of its own
// world (coop/props/prop_spawn_authoring) that never crossed, its cleaning a fall the host's judge refuses for want of a
// sponge (coop/interactables/grime_judge), and the host's copy, moved by the client's rows, never poured. Now, on
// BucketPourAsk:
//   CLIENT -- a blob born near a bucket this client moves, and not a host blob's copy (bound to an element by the next
//             frame), is sent to the host with its place and velocity and removed here.
//   HOST   -- judges it (the bucket in the sender's grab, the blob within 3 m of the host's copy, sixty a second),
//             spawns the blob itself with that velocity, takes its water to the client copy's level (2 to 6 a blob) and
//             marks the bucket's row.
// The host's blobs cross to every peer as the host's own pours always have; their cleaning is the host's. Game thread.

#pragma once

namespace coop::net {
class Session;
struct BucketPourAskPayload;
}  // namespace coop::net

namespace coop::bucket_pour {

void Install(coop::net::Session* session);

// Settles the watch; CLIENT: sends the blobs born since the last frame.
void Tick();

void OnAsk(int senderSlot, const coop::net::BucketPourAskPayload& p);  // HOST

// The blobs asked and spawned, for the drill. CLIENT: asked; HOST: spawned and refused.
struct Counts {
    unsigned asked = 0, spawned = 0, refused = 0;
};
Counts GetCounts();

void OnDisconnect();

}  // namespace coop::bucket_pour
