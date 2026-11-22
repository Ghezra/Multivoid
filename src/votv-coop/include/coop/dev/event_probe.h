// coop/dev/event_probe.h -- [dev] what an event leaves in the world on each peer (VOTVCOOP_EVENT_PROBE=<event> and
// VOTVCOOP_EVENT_PROBE_CLASS=<substring>[|<substring>...] on both peers): the host forces the event, both list the
// matching actors. VOTVCOOP_EVENT_PROBE_UNPASS=1 takes a past row out of every peer's passed events first.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::event_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::event_probe
