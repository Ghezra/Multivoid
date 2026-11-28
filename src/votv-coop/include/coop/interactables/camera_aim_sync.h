// coop/interactables/camera_aim_sync.h -- a security camera points where the player turning it pointed it, on every peer.
//
// A camera's aim is three targets, `rot_x`, `rot_y` and `zoom`, which setAngle and addAngle (the laptop's controls)
// write and move() turns the head and the lens toward; each machine turned its own copy only (Ghezra/Multivoid issue
// 60). Now, on CameraAimAsk and CameraAimState:
//   CLIENT -- its own turn runs at once; after it, the camera's targets go to the host, at most ten times a second.
//   HOST   -- writes a client's targets on its camera and runs move(); after its own turn, or a client's, sends the
//             targets to every other player at most ten times a second, and to a joiner every camera turned.
//   CLIENT -- writes the host's targets and runs move(). Game thread.

#pragma once

namespace coop::net {
class Session;
struct CameraAimPayload;
}  // namespace coop::net

namespace coop::camera_aim_sync {

void Install(coop::net::Session* session);
void Tick();

void OnAsk(int senderSlot, const coop::net::CameraAimPayload& p);  // HOST
void OnState(const coop::net::CameraAimPayload& p);                // CLIENT

// HOST: slot `slot`'s connect replay: every camera turned this session, to it alone.
void SendJoinSnapshotForSlot(int slot);

void OnDisconnect();

}  // namespace coop::camera_aim_sync
