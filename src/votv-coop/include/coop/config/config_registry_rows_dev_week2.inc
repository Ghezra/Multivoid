// coop/config/config_registry_rows_dev_week2.inc -- the [dev] row of the story's second week's drill: its red arms.
// Included by config_registry_rows_dev.inc after its last row, as _dev_ambient.inc is, so each row keeps its place in
// every expansion of the list. The entry forms are config_registry_rows.inc's.

CFG_ENUM(week2_red, "week2_red", "dev", "off", "off|shrimp_open|ship_mail|no_ships|no_host_event|rep_open", "VOTVCOOP_WEEK2_RED",
    "Developer red arm of the week two drill: never set; set on EVERY peer. shrimp_open: a client's Ariral shrimp eater "
    "runs and the host's ignores the clients' players, so a shrimp beside a client must FAIL by going. ship_mail (the "
    "client): its Ariral ship's fade end mails, so its inbox must FAIL with the OUTSIDE emails twice. no_ships (the "
    "host): the picnic's warp arrows are not sent, so a client must FAIL with none. no_host_event (the host): a host-only "
    "event's count is not sent, so a client must FAIL to count the knocker. rep_open: every peer keeps its own Ariral "
    "reputation, so the peers' values must FAIL to agree.")
