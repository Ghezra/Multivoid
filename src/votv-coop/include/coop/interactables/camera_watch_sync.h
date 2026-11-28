// coop/interactables/camera_watch_sync.h -- a security camera's green light says whether any player watches it.
//
// A camera's light is green while its `spec` is set, which the game sets in the laptop's setCam on the camera picked
// and clears on the one left, on that machine alone: another player's watching never showed, and leaving the cameras
// panel or picking a drone or a Kerfur left the last camera green (Ghezra/Multivoid issue 57). Now, on the
// CameraWatchAsk and CameraWatchState kinds:
//   EVERY PEER -- looks four times a second at what its own player watches: a laptop open on its cameras panel with a
//                 camera selected, or nothing.
//   CLIENT     -- sends its watched camera when it changes (none included).
//   HOST       -- keeps each player's camera, its own too; a camera is watched while any of them watches it. It writes
//                 `spec` on its own camera and runs upd(), and sends each change to every client, every watched camera
//                 to a joiner.
//   CLIENT     -- writes the host's `spec` and runs upd(); after its own laptop's setCam, which writes `spec` on its
//                 copies, the host's bits are written back. Game thread.

#pragma once

namespace coop::net {
class Session;
struct CameraWatchAskPayload;
struct CameraWatchStatePayload;
}  // namespace coop::net

namespace coop::camera_watch_sync {

void Install(coop::net::Session* session);
void Tick();

void OnAsk(int senderSlot, const coop::net::CameraWatchAskPayload& p);  // HOST
void OnState(const coop::net::CameraWatchStatePayload& p);             // CLIENT

// HOST: slot `slot`'s connect replay: every watched camera, to it alone.
void SendJoinSnapshotForSlot(int slot);

// HOST: a slot that left watches nothing.
void OnPeerLeftSlot(int slot);

void OnDisconnect();

}  // namespace coop::camera_watch_sync
