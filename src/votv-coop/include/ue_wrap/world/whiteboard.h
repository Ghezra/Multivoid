// ue_wrap/world/whiteboard.h -- a placed whiteboard and the picture its drawing surface holds.
//
// 0.9.0n. prop_whiteboard_C (a prop_wallattachable_C) carries a `whiteboardobj` child actor of the native class
// whiteboard_C, which owns the drawing: a render target (`rt`) the player paints into while a stroke runs, the stroke's
// own math (lineTrace, transformPoint, makeTris), and the game's own serialization of the finished picture --
// `TextureToString` (render target -> base64 text) and `stringToTexture` (text -> render target). prop_whiteboard_C's
// `getData` banks that text in the save slot, so the text is the picture's identity on both ends.
//
// Engine-wrapper layer (principle 7): reads and the game's two calls, no network and no coop state. Game thread only.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ue_wrap::whiteboard {

// Every placed whiteboard in the world, up to `max`. A full object-array walk: call it on a timer, never per frame.
void FindBoards(std::vector<void*>& out, size_t max);

// The prop's drawing surface (its `whiteboardobj` child actor), or null. `IsBoard` says whether the prop is one.
bool IsBoard(void* prop);
void* SurfaceOf(void* prop);

// Whether the surface holds a render target, so its picture can be read and written right now.
bool CanRead(void* surface);

// The whole picture as the game serializes it: the base64 text, "" when the surface or its call does not resolve.
std::string PictureOf(void* surface);

// Puts a picture back through the game's own deserializer, `stringToTexture`. False when it does not resolve or the call
// does not run.
bool ApplyPicture(void* surface, const std::string& picture);

// Whether a stroke is being drawn on the surface at this instant (the surface's `Active`).
bool IsActive(void* surface);

// A stable, cross-peer identity for a board: FNV-1a 64 over its full object path.
uint64_t KeyOf(void* prop);

}  // namespace ue_wrap::whiteboard