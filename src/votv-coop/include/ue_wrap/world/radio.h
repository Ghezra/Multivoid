// ue_wrap/world/radio.h -- the placed internet radio (prop_radio_internet_C) and its media player.
//
// A track crosses as a hash of its catalog link and is opened only from this peer's own catalog, so no peer can make
// another open an address or a path of its choosing. The ATV's radio is a subclass with its own owner and is left out.
// Engine-wrapper layer: no network. Game thread.

#pragma once

#include <cstdint>
#include <string>

namespace ue_wrap::radio {

inline constexpr const wchar_t* kClass = L"prop_radio_internet_C";

struct State {
    uint64_t track = 0;  // 0: nothing selected
    int64_t  ticks = 0;  // the player's time, 100 ns
    bool     playing = false;
    uint8_t  mode = 0;   // playmode, 0 to 3
};

bool IsRadio(void* actor);

// Stable across installation roots: lower case, forward slashes, the part under radio/ for a file. Never 0.
uint64_t TrackId(std::wstring link);

// False while the player is opening a track, or when the actor is not a radio.
bool Read(void* actor, State& out);

enum class Apply : uint8_t { Done, Waiting, NoSuchTrack };

// Move the radio to `state`: its own openLink for another track, which finishes later, then its time, play or pause and
// mode. NoSuchTrack when this peer cannot open it, and then it is paused: its catalog has no such track, or its Custom
// Content rule is off, which openLink refuses with a hint on every call, so it is never called then.
Apply ApplyState(void* actor, const State& state);

// The track at `index` of this peer's catalog, 0 past its end.
uint64_t CatalogTrack(void* actor, int index);

// The radio this peer's player has its interface open on, or null.
void* RadioWithOpenInterface(void* player);

}  // namespace ue_wrap::radio
