// coop/dev/kdisc_watch.h -- [dev] where every floppy disc is (VOTVCOOP_KDISC_WATCH=1, on both peers): each disc that
// moved, or changed what holds it, is logged every 2 s.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::kdisc_watch {

// Per frame. A latched read when the watch is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::kdisc_watch
