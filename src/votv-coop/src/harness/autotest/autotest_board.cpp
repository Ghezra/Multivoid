// harness/autotest/autotest_board.cpp -- whiteboard picture transport end to end, without a hand on the board.
//
// HOST: after the world settles it forces the lane's join answer to slot 1, which sends every board's picture down the
// same path a joiner gets.
// CLIENT: waits, then PASSes when the lane applied at least one picture (`whiteboard_sync::AppliedCount`), FAILs after a
// window without one.
// With [dev] board_drill_off (VOTVCOOP_BOARD_DRILL_OFF=1) the host's answer is skipped, so the client applies nothing and
// the drill goes red: the switch proves the drill can fail.

#include "harness/autotest.h"

#include "coop/cheats/cheat_request.h"
#include "coop/world/whiteboard_sync.h"
#include "coop/net/protocol.h"
#include "ue_wrap/core/log.h"

#include <windows.h>

namespace harness::autotest {
namespace {

constexpr DWORD kHostSettleMs = 4000;    // the world up, the join gate drained
constexpr DWORD kClientWindowMs = 90000;

void HostLeg() {
    ::Sleep(4000);
    // Wait for the peer rather than a fixed instant: the drill is run on two launches that do not start together.
    for (int i = 0; i < 120; ++i) {
        if (coop::whiteboard_sync::SlotReady(1)) {
            const uint64_t before = coop::whiteboard_sync::SentCount();
            coop::whiteboard_sync::ForceJoinAnswer(1);
            ::Sleep(500);
            UE_LOGI("board_drill: HOST forced the join answer to slot 1 (%llu sent, %llu before)",
                    static_cast<unsigned long long>(coop::whiteboard_sync::SentCount()),
                    static_cast<unsigned long long>(before));
            return;
        }
        ::Sleep(1000);
    }
    UE_LOGI("board_drill: HOST FAIL -- slot 1 never became world-ready");
}

void ClientLeg() {
    ::Sleep(6000);
    const DWORD until = ::GetTickCount() + kClientWindowMs;
    while (::GetTickCount() < until) {
        if (coop::whiteboard_sync::AppliedCount() >= 1) {
            UE_LOGI("board_drill: CLIENT PASS -- a board picture crossed and applied here");
            return;
        }
        ::Sleep(250);
    }
    UE_LOGI("board_drill: CLIENT FAIL -- no board picture applied in %lu s", kClientWindowMs / 1000);
}

}  // namespace

void RunBoardDrill() {
    if (IsClientRole()) ClientLeg();
    else HostLeg();
}

DWORD WINAPI BoardDrillThread(LPVOID /*arg*/) {
    RunBoardDrill();
    return 0;
}

}  // namespace harness::autotest