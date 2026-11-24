// ue_wrap/actors/photo_store.h -- the save object's photos: the laptop's gallery and the printed photos' pictures.
//
// UsaveSlot_C keeps two arrays of Fstruct_byteImage (one TArray<uint8> each, a JPEG): `photos`, the gallery the
// gamemode's `photo` appends every capture to, and `photos_phys`, the picture of each printed photo, where the camera's
// makePhoto copies the gallery's last entry to the same index. A prop_photo_C names its picture by that index (its
// `index`, saved in its record), and its loadImage reads photos_phys[index]. Nothing in the game removes from either
// array. Engine-wrapper layer: field reads and engine-owned array writes, no network logic. Game thread.

#pragma once

#include <cstdint>
#include <vector>

namespace ue_wrap::photo_store {

// The gallery's (`phys` false) or the printed pictures' (`phys` true) length, or -1 while the save object is unresolved.
int32_t Count(bool phys);

// A copy of the gallery's (`phys` false) or the printed pictures' (`phys` true) entry at `index`. False when the save
// object is unresolved, the index is past the array or the entry is empty.
bool Read(bool phys, int32_t index, std::vector<uint8_t>& out);

// Append `bytes` to the gallery; the new entry's index, or -1 (nothing written).
int32_t AppendGallery(const std::vector<uint8_t>& bytes);

// Write `bytes` as the printed picture at `index`, the array grown with empty entries to reach it. The entry it
// replaces is released to the engine. False, having written nothing, when the save object is unresolved, the index is
// negative or past the bound, or an allocation fails.
bool WritePhys(int32_t index, const std::vector<uint8_t>& bytes);

}  // namespace ue_wrap::photo_store
