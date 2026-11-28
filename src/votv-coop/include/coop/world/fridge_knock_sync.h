// coop/world/fridge_knock_sync.h -- the basement fridge's knocks are the host's, heard by every player.
//
// The basement fridge (fridgeGlow_C) knocks two ways, each from its own timer and dice: past day 14 its timer_3knock
// may run startKnocking once a session (three loud knocks, each with a world camera shake at the fridge, then the
// figure), and its timer_removeObscurity may run knockObscurity (a quieter knock that pushes the props against the
// door). Each peer rolled its own dice against its own player, so a client never heard the host's knocks nor felt the
// shake (Ghezra/Multivoid issue 61). Now, on EventCue 133 and 134:
//   HOST   -- its timers run; a knock that started (`knocked` set, or `knockamount` spent) is sent as it starts.
//   CLIENT -- its timers are refused. The big knock runs its own startKnocking (the knocks, the shake on its own
//             player, the figure); the quiet knock plays its own knock sound at the host's volume and pitch. The props
//             the quiet knock pushes are the host's, on their own lane. Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct EventCuePayload;
}  // namespace coop::net

namespace coop::fridge_knock_sync {

// The EventCue ids; the cue registry's ids are append-only.
inline constexpr uint32_t kCueKnocking = 133;   // the big knock starts
inline constexpr uint32_t kCueObscure = 134;    // the quiet knock: x its volume, y its pitch

void Install(coop::net::Session* session);

// The watches, retried until registered. Game thread, once a tick.
void Tick();

// A client's receiver: true when the cue was this lane's.
bool OnCue(const coop::net::EventCuePayload& p);

void OnDisconnect();

}  // namespace coop::fridge_knock_sync
