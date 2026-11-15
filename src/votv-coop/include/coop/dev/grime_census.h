// coop/dev/grime_census.h -- [dev] the world's grime decals counted per class with their summed dirt
// (VOTVCOOP_GRIME_CENSUS=1), for Ghezra/Multivoid issue 36.
#pragma once

namespace coop::dev::grime_census {

// Per frame, from the dev lanes; no session needed. A latched read when off.
void Tick();

}  // namespace coop::dev::grime_census
