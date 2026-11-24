// coop/props/custom_picture_sync.h -- a custom picture's choice is the host's, by its file name.
//
// A picture frame (prop_picture_h_C and its sizes), a held poster (prop_poster_C), a poster on a wall (poster_C), a held
// custom rug (prop_rug_c_C) and a laid one (rug_C) open, with the world's custom content on, a picker over the
// player's own custom pictures; its pick (intComs_texturePickerApply) sets the piece's `filename` and paints it on that
// machine alone. Now, on the PictureAsk and PictureState kinds (a prop by its element id, a wall poster or laid rug by
// its save key):
//   CLIENT -- its pick is sent to the host as the piece and the file name, never the picture.
//   HOST   -- judges it (the piece within 8 m of the sender's body, four asks a second), sets its own piece's name, paints
//             it with the piece's own function (upd, setTex, setPoster, setSkin) and sends the name to every client; its
//             own picks go the same way, and a joiner gets every named frame, held poster and held rug (wall posters and
//             laid rugs come with their build records). A refused pick is answered with the host's name.
//   CLIENT -- sets its copy's name and paints it, the picture loaded from its own custom folder.
// Every player is taken to have the same custom picture files. Game thread.

#pragma once

namespace coop::net {
class Session;
struct CustomPicturePayload;
}  // namespace coop::net

namespace coop::custom_picture_sync {

void Install(coop::net::Session* session);
void Tick();

void OnAsk(int senderSlot, const coop::net::CustomPicturePayload& p);  // HOST
void OnState(const coop::net::CustomPicturePayload& p);                // CLIENT

// HOST: slot `slot`'s connect replay: every frame and held poster with a picture, to it alone.
void SendJoinSnapshotForSlot(int slot);

void OnDisconnect();

}  // namespace coop::custom_picture_sync
