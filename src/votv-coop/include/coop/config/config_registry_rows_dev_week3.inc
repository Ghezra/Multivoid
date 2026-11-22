// coop/config/config_registry_rows_dev_week3.inc -- the [dev] row of the story's third week's drill: its red arms.
// Included by config_registry_rows_dev.inc after its last row, as _dev_week2.inc is, so each row keeps its place in
// every expansion of the list. The entry forms are config_registry_rows.inc's.

CFG_ENUM(week3_red, "week3_red", "dev", "off", "off|tower_open|note_local", "VOTVCOOP_WEEK3_RED",
    "Developer red arm of the week three drill: never set; set on EVERY peer. tower_open: every peer's radio-tower "
    "Ariral ship is its own, so a ship at a client's player must FAIL to show on the host. note_local (the client): "
    "the lantern prank's notepad runs its use on the client, so the Ariral it sends must FAIL to show on the host.")
