// coop/dev/menu_spawn_drive.h -- [dev] a probe's spawn of a catalog row through the spawn menu's own spawn(FName), the
// function a click on a menu slot calls (ui_spawnmenu_C::spawn -> gamemode->spawnPropThroughGamemode), so a client's
// birth is the one a player's click makes and crosses as the click's would (coop/props/prop_spawn_authoring). The
// widget need not be open; spawn() traces from the player's camera, so the prop lands where the player looks. The
// same steps as harness/autotest/autotest_menuspawn.cpp. Game thread.
#pragma once

#include <string>
#include <vector>

namespace coop::dev::menu_spawn_drive {

// Whether the menu's widget and its spawn() resolve in this world (the propProcessor makes it during the world's
// startup, so a client can stand in its world before it exists).
bool Ready();

// Whether the game's catalog (propProcessor_C::propsNames) holds `row`.
bool CatalogHas(const std::wstring& row);

// The catalog's rows whose name holds `part`, case ignored, at most `cap`.
std::vector<std::wstring> RowsContaining(const std::wstring& part, size_t cap = 16);

// The class `row` spawns as (lib_C's Prop To Object), or null.
void* RowClass(const std::wstring& row);

// The menu's spawn() run with `row`. False when the widget or the function does not resolve or the call did not run.
bool Spawn(const std::wstring& row);

}  // namespace coop::dev::menu_spawn_drive
