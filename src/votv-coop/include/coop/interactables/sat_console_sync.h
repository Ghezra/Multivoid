// coop/interactables/sat_console_sync.h -- the SAT console's shared commands run on the host.
//
// A machine has one terminal, the ui_console every SAT console panel shows. A client's line whose command
// rests on the shared world (coop/interactables/sat_console_table) is refused at its terminal's enterCommand
// while the terminal is not busy, its input consumed as the body's tail does, and sent to the host with the
// terminal's context: its dish or ROOT, its name, the panel last used. The host runs it on a terminal of the
// game's class it keeps for each typist, by identity, never shown, so the command happens once, in the
// host's world. Each line that terminal prints goes to its typist with its busy level, and to every other
// client and the host's own terminal; a line the host's own terminal prints goes to every client, so every
// screen shows what any player ran. A typist that leaves mid-command leaves it running and is bound again
// at its world-ready; the last client's leave discards every terminal. MTA runs a command the client does
// not own on the server, as that player (reference/mtasa-blue/Server/mods/deathmatch/logic/CGame.cpp:2406),
// but echoes it to that player alone, and runs the client's own handlers first
// (reference/mtasa-blue/Client/mods/deathmatch/ClientCommands.cpp:60), which here would run the one game
// handler twice: a line runs on one machine, and its echo goes to every player.
#pragma once

#include <cstddef>
#include <cstdint>

namespace coop::net {
class Session;
struct BlobChunkPayload;
}

namespace coop::sat_console_sync {

void Install(coop::net::Session* s);
void Tick();
void OnChunk(const coop::net::BlobChunkPayload& p, uint8_t senderSlot);
// A peer's world is ready: a typist back at a terminal still running is bound to it again. A peer's slot
// is gone: its chunks and owed blobs are forgotten; on the host its terminal is kept.
void OnPeerWorldReady(int slot);
void OnPeerGone(uint8_t slot);
void OnDisconnect();

// [dev] What the lane did, for the drill: the lines this client sent to run on the host, and on the host
// the lines it ran for clients, the lines those runs printed back, the typists bound again to a terminal
// they had left, and those whose terminal was busy then; and the terminals the host keeps.
struct Counts {
    uint64_t linesSent = 0;
    uint64_t linesRun = 0;
    uint64_t linesReturned = 0;
    uint64_t linesShown = 0;   // HOST: lines sent to a client that did not type them
    uint64_t rebinds = 0;
    uint64_t busyRebinds = 0;
};
Counts LaneCounts();
size_t KeptTerminals(void** out, size_t cap);

}  // namespace coop::sat_console_sync
