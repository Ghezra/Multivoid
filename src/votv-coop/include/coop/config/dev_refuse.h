// coop/config/dev_refuse.h -- [dev] a lane's game call refused on purpose, the red arm of the timed client loops that
// act on a pass (VOTVCOOP_DEV_REFUSE=alarm,media,fuse,kcam,buildlose,nail,cord, any of them). Each names one call a
// client loop makes; refused, the call is skipped (or its effect undone) as the game itself might, and every refusal
// is counted on one log line, so a run shows how often the loop asked again. Off: one latched read per name.
#pragma once

#include "coop/config/config.h"
#include "ue_wrap/core/log.h"

#include <string>

namespace coop::dev_refuse {

// Whether `name` is in VOTVCOOP_DEV_REFUSE.
inline bool Armed(const char* name) {
    static const std::string list = "," + coop::config::ReadEnv("VOTVCOOP_DEV_REFUSE") + ",";
    return list.size() > 2 && list.find("," + std::string(name) + ",") != std::string::npos;
}

// Armed(name), and when it is, the refusal counted and said with its number. The counter is the caller's, one static
// per call site.
inline bool Refuse(const char* name, unsigned& count) {
    if (!Armed(name)) return false;
    ++count;
    UE_LOGI("[REFUSE] %s refused (#%u)", name, count);
    return true;
}

}  // namespace coop::dev_refuse
