// coop/dev/satlog_probe.h -- [dev] what each peer's SAT console screen shows, for the Trello card "server room console
// screen desync".
//
// VOTVCOOP_SATLOG_PROBE=1, run with sat_console_drill (a client types at the console). Every peer, each 5 s from 20 s
// to 180 s: the length of its terminal's printed log and its last 120 characters (`[SATLOG]`), so a peer whose screen
// never shows another player's lines is a diff of three logs.

#pragma once

namespace coop::dev::satlog_probe {

void Tick();

}  // namespace coop::dev::satlog_probe
