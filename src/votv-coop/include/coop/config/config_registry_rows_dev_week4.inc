// coop/config/config_registry_rows_dev_week4.inc -- the [dev] row of the story's fourth week's drill: its red arms.
// Included by config_registry_rows_dev.inc after its last row, as _dev_week3.inc is, so each row keeps its place in
// every expansion of the list. The entry forms are config_registry_rows.inc's.

CFG_ENUM(week4_red, "week4_red", "dev", "off", "off|obelisk_host|box_local", "VOTVCOOP_WEEK4_RED",
    "Developer red arm of the week four drill: never set; set on EVERY peer. obelisk_host (the host): the warning "
    "obelisk's fall is not sent, so a client must FAIL to see it fall. box_local (the client): its own copy of the "
    "garage box runs, so its walk-in must FAIL with a gun of its own.")
