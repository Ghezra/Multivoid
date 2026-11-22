// coop/config/config_registry_rows_dev_week5.inc -- the [dev] row of the story's fifth week's drill: its red arm.
// Included by config_registry_rows_dev.inc after its last row, as _dev_week4.inc is, so each row keeps its place in
// every expansion of the list. The entry forms are config_registry_rows.inc's.

CFG_ENUM(week5_red, "week5_red", "dev", "off", "off|scouts_host|solto_host", "VOTVCOOP_WEEK5_RED",
    "Developer red arm of the week five drill: never set; set on EVERY peer. scouts_host (the host): the Rozital "
    "scouts hunt and pin the host alone, so a client beside them must FAIL to be pinned. solto_host (every peer): the "
    "garage Soltomia is the host's alone, so a client must FAIL to see it.")
