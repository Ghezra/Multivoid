// coop/dev/laptopboot_probe.h -- [dev] a joiner boots the laptop with a disc in it, and every peer counts its files.
//
// VOTVCOOP_LAPTOPBOOT_PROBE=1, run with laptop_drill=join (the host puts a disc in its laptop in the joiner's window).
// Slot 1, from 50 s after its session connects: the laptop's power pressed as the button does (actionOptionIndex 8),
// on if it is off, then at 70 s off and at 85 s on again. Every peer, each 3 s from 20 s to 140 s: the laptop's power
// and the file-buffer quad's counts (floppyData, floppyBuffer, floppyBufferUIDs) and floppyReadwrites (`[LAPBOOT]`), so
// a doubled file list or read count shows as a peer's counts leaving the host's.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::laptopboot_probe {

void Tick(coop::net::Session* session);

}  // namespace coop::dev::laptopboot_probe
