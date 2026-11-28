// coop/interactables/camera_screen_sync.h -- a security camera's name, and the camera a TV shows, are the host's.
//
// A camera's name (`cameraName`) is written by the laptop's camera slot when its name box is committed, and a TV
// shows a camera through its interface's search box: bindCamera(name) finds the camera of that name and setAsSecurity
// turns the TV to it. Each ran on its own machine alone, so a renamed camera kept its old name on the others and a TV
// showed its camera on its own machine alone (Ghezra/Multivoid issue 54). Now, on CameraNameAsk/State and
// TvCameraAsk/State:
//   CLIENT -- its own rename or TV pick runs at once; after it, the camera's name or the TV's camera goes to the host.
//   HOST   -- writes a client's name on its camera, or binds its TV to that camera; after its own change or a client's,
//             sends it to every other player, and every name and TV changed this session to a joiner.
//   CLIENT -- writes the host's name, or binds its own TV to its own copy of the camera by that copy's name.
// Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct CameraNamePayload;
struct TvCameraPayload;
}  // namespace coop::net

namespace coop::camera_screen_sync {

void Install(coop::net::Session* session);
void Tick();

void OnNameAsk(int senderSlot, const coop::net::CameraNamePayload& p);  // HOST
void OnNameState(const coop::net::CameraNamePayload& p);                // CLIENT
void OnTvAsk(int senderSlot, const coop::net::TvCameraPayload& p);      // HOST
void OnTvState(const coop::net::TvCameraPayload& p);                    // CLIENT

// HOST: slot `slot`'s connect replay: every camera renamed and every TV bound this session, to it alone.
void SendJoinSnapshotForSlot(int slot);

void OnDisconnect();

}  // namespace coop::camera_screen_sync
