// coop/dev/puppet_blast_probe.h -- [dev] a blast at a peer's puppet on the rig (VOTVCOOP_PUPPET_BLAST_PROBE=1), for
// VOTV-MP/Multivoid issues 103, 65 and 46: the client stands away from the host, the host sets off an explosion at the
// client's puppet, and both log their own health.
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::puppet_blast_probe {

// Per frame. A latched read when the probe is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::puppet_blast_probe
