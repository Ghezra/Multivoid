// coop/player/backrooms_guard.cpp -- see coop/player/backrooms_guard.h.

#include "coop/player/backrooms_guard.h"

#include "coop/net/session.h"

#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/world/world_singleton.h"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace coop::backrooms_guard {
namespace {

namespace R = ue_wrap::reflection;
using Clock = std::chrono::steady_clock;

constexpr auto kPoll = std::chrono::milliseconds(250);

std::atomic<coop::net::Session*> g_session{nullptr};
Clock::time_point g_nextPoll{};
bool g_turnedOff = false;

// Writes the flag and says what it was; false with no gamemode.
bool WriteFlag(bool on, bool& was) {
    void* gm = ue_wrap::world_singleton::Gamemode();
    int32_t off = -1;
    uint8_t mask = 0;
    if (!gm || !R::IsLive(gm) || !R::FindBoolProperty(R::ClassOf(gm), L"backroomsEnabled", off, mask)) return false;
    uint8_t& b = static_cast<uint8_t*>(gm)[off];
    was = (b & mask) != 0;
    b = on ? (b | mask) : (b & ~mask);
    return true;
}

}  // namespace

void Install(coop::net::Session* session) { g_session.store(session); }

void Tick() {
    if (!g_session.load()) return;
    const Clock::time_point now = Clock::now();
    if (now < g_nextPoll) return;
    g_nextPoll = now + kPoll;
    bool was = false;
    if (WriteFlag(false, was) && was) {
        g_turnedOff = true;
        UE_LOGI("backrooms_guard: the way into the backrooms is off for this session");
    }
}

void OnDisconnect() {
    bool was = false;
    if (g_turnedOff) WriteFlag(true, was);
    g_turnedOff = false;
    g_session.store(nullptr);
}

}  // namespace coop::backrooms_guard
