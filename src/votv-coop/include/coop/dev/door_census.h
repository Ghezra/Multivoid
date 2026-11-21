// coop/dev/door_census.h -- [dev] every door's state on every peer (VOTVCOOP_DOOR_CENSUS=1), for the Trello card
// "inaccessible doors in main facility and satellite".
#pragma once

namespace coop::net { class Session; }

namespace coop::dev::door_census {

// Per frame. A latched read when the census is off.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::door_census
