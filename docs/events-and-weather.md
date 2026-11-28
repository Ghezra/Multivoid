# Events and weather

## Purpose

The shared world's own progression: the clock and the sky, the weather, the ambient spawners
and who is allowed to roll them, the story and scheduled events and how a client sees one fire,
the actors an event flies in, the walking pyramid, the base alarm, the emails and the daily
task. The rule under all of it: shared-world randomness is rolled once, on the host, and a
client never rolls a shared outcome.

## How it works

### The clock and the sky

The game's clock lives on one actor, the day-night cycle, and the sun is a pure function of it. The
host streams its clock -- the within-day time, the absolute time and the day number. On a client,
right before each tick of its world's cycle (the menu scene's own cycle is left alone), the lane
holds the cycle's rate at zero and writes the host's last sample in, a newly arrived one or else
the last one again, so the client's clock advances only with the host's and neither its own advance
nor a write on the client (its cheat menu's day buttons, which do nothing there) reaches a
midnight: the midnight's shared outputs (the dish hash codes, the daily task, the Bad Sun roll, the
results mail) are the host's, and the host's midnight reaches a client as one sample carrying the
next day number `[V]` (`coop/world/time_sync`). At that sample a client performs its own share of
the rollover, the part each machine keeps for its own player: its profile's days lived, its count
of midnights since its world loaded (the insomniac achievement at seven), the day's music flags set
again, and game mode 0's day-number achievements `[RD]` (`coop/world/day_edge`). Its hour pulses
still run, so the game's automatic 6 am drone order is latched on a client: its slot can load the
flag open from the save, and the midnight that re-opens it daily is the host's alone; the weather
births and the event walk are held too (below), and the sky eye is the host's (a client's own noon
roll is refused) `[V]`, and so are the jellyfish (a client's own 18:00 spawn is refused) `[V]` and
the red sky (a client's own toggle is refused, its start and its end alike: `[V]` on a reflected
call, the noon's own route `[RD]` by the gate's every-route design), while the flesh rain and the
gifts run per peer (the known limits). Game mode 5 resets the time
of day every second on every machine; a client refuses its own copy of that loop at the loop's
once-a-second resume, after the one pass its begin-play runs, which the clock takes back `[V]`, and
the mode's other flows run on: its needs restore `[V]`, its ambience and spawners `[RD]` (measured
on a master spawned in a joined world; one a mode-5 world loads with is `[RD]`). A sample is sent
each time the host's clock has moved half a game minute, and at least twice a second: every 500 ms
at the normal speed, where a game minute holds about six, so a client's own minute and hour pulses,
its sun, sounds, decals and weekday, follow the host's minute for minute `[RD]`; in the shared
sleep (on [players.md](players.md)) one comes about every 80 ms `[RD]`, and a frame longer than
that on either peer can merge two and skip a minute's pulse `[RD]` -- one night gave 44 of 45
`[V]`. The star dome's random orientation, the save-derived moon phase and the sky eye stream from
the host once a second (`coop/world/sky_sync`) `[V]`. So does the earth teleport's black hole: the event's `tp` hides the
day sky and shows the black hole 20 s in and turns them back 86 s later, unless a game rule keeps the black hole. A
client refuses its own `tp`, takes the host's flip through the sky's own `skyVisibility` and the black hole's light,
and reads the emails from the host's feed; its two 2D sounds are carried as EventCue ids 100 and 101 (`coop/world/world_sound_sync`). A joiner gets the flip with the sky
(`harness/autotest/autotest_sky_tp.cpp`, `sky_tp_drill`) `[?]`: the client's sky flips and turns back with the host's, and a
client that joins mid-flip gets the black hole at its join (the drill's first rig run is pending).

### Weather

One actor owns the weather: its scheduler timers for rain, lightning, fog, super fog and
permanent rain, a dozen state fields, and the mutator functions everything else calls. On the
host, observers on the five scheduler functions read the state after each fires, dedupe it by
hash and broadcast it on change and at a joiner's connect (`coop/world/weather_sync`). On a
client the same five scheduler bodies are cancelled before they run, so the client never rolls
"rain now", and a received state is applied through the mutator functions rather than field
writes, because the snow function alone has dozens of Blueprint listeners (`coop/world/weather_rain`).

Fog is not a flag: the enable flags are config gates, and the active fog is an actor (the rolling
fog controller, the super fog) that ramps the height-fog density over its own duration and
destroys itself; the host mirrors the actor's life (`coop/world/weather_fog`). A lightning
strike is a transient actor; the host observes its spawn and sends the location
(`coop/world/weather_lightning`). The red sky is a story event actor that the clock's hour pulse
toggles at noon on every peer -- a living one ends, else a one-percent roll starts one --
dispatched inside the Blueprint, where the script-body gate still sees the toggle: the host sends
its red sky as each toggle leaves it, and a client refuses its own and runs the host's through
the same toggle (`coop/world/weather_redsky`). The Bad Sun is a story event actor too, but every
peer runs its own copy by the shared clock and the harm is local: its widget traces from the local
camera and hurts the local player. The host owns whether a copy exists -- it sends its state as
`Spawn Bad Sun` returns and as its copy's own `remove()` returns (the midnight roll stays the host's,
and now crosses). A client's own `Spawn Bad Sun` is refused at the script-body gate, and a client
keeps exactly one copy: the host's present spawns one if none lives, and the host's absent destroys
an inactive copy at once and leaves an active one to fade by its own clock
(`coop/world/weather_badsun`). The clock half is `[V]` (a client's own copy activates at 07:00); the
wire half is `[V]` (the lane's drill: a spawn, a refused client spawn, the end, a rejoin and a joiner's
stale copy, each with its red), the day cycle's midnight route `[V]`, and the cheat menu's `[RD]` (its
handler needs a Sandbox or flying world). The black fog and the rolling fog the clock rolls
funnel through the engine's finish-spawning call, where a client-side catch destroys any birth
the host did not command (`coop/world/weather_event_births`). The black fog's whole show plays round the local
camera and runs on its own clock for about three minutes, so the host's weather state carries whether one lives: a
client spawns its own copy at the host's start, or at a join while it runs, and destroys it at the host's end; the
eyes its timer spawns stay the host's, a client's copy refused them (`coop/world/weather_fog`) `[V]` (`coop/dev/blackfog_probe`:
the client's fog began and ended in the same second as the host's). The rolling fog's density follows a noise its controller seeds at random on
each peer; the host's state carries its time, pace and duration too, which a client writes on its copy (issue 55).

Fireflies are the one peer-symmetric weather: the spawner rolls a ring around the local camera,
so every peer keeps its own and shares each spawn, and the union is fireflies near everyone
(`coop/world/firefly_sync`).

### Who rolls

A connected client ticks no shared-world spawner and rolls no shared-world spawn
(`coop/world/spawn_authority`), by one table of the bodies a client must not run, a spawner's
tick or its entry function, each refused at the script gate by a watch on its class and function
names while a client session runs. Shared-world content then arrives only from the host
through the character, world-actor and prop mirrors. A spawner anchored to the player's camera
(the pinecone drops) is the exception: each peer rolls its own; the host's drops reach the
others as keyed props, and a client's stay its own. The remaining randomness is closed shape by shape:
a mirror where the host owns the roll and the state, an intent where a client's action makes
the host roll (a shop delivery time, a signal catch, a device claim), and a shared seed where
the game already uses a seeded stream. Cosmetic-local randomness (view bob, flicker, exhaust)
is left alone.

The gamemode's night roll is the host's. It is a one-shot timer, re-armed every two to five minutes, that in a
night from the fourth day rolls a three-percent chance of a visitor: a Geom Octa four times in ten, else a night
egg. A client's body of it is refused at the script gate, which also ends its own re-arm, so a client never mints
either; the host's roll is a spawn source of the character lane, and the Octa or the egg reaches every client as
a mirror `[V]` (`octa_drill` births). The Octa's attack on a player is relayed from the host (`docs/npcs-and-kerfur.md`).

### Story and scheduled events

The scheduler is the save's own time step: it walks the event table and runs each due row
through the eventer, all inside Blueprints. The host sees each fire at the verbs: script-body
watches on the eventer's `runEvent` and `runSpecialEvent` broadcast every call whose body completed --
a scheduled row's, a dev-menu fire's, the game's own menus', and, for the prank roll, the case
`summonArirPrank` actually picked, one broadcast per occurrence (`coop/world/event_fire_sync`).
The client keeps its own copy of the walkable event list empty, a one-integer write held right
before each tick of the clock's cycle, which the game rebuilds unconditionally at every world
load, so its scheduler fires nothing as the host's clock moves it. What a client does with a
received fire is a per-row policy kept in the code: rows whose outputs already ride a lane
(props, creatures, the ATV, sleep, the wisps, the cues, the devices) are not replayed, because
replaying them would deliver the effect twice; the level flips, story flags and cosmetic sounds
no lane carries are replayed through the same native verb. A prank's rolled case arrives as its
own `runSpecialEvent` fire and meets the same policy: a graffiti decal, a scare box's arm or the
alien sounds' noisers (each player's own, around them) are replayed, the outcome the host rolled;
a case whose output rides a lane or stays on the host is not. A client runs no eventer verb of its own: the game's event and cheat menus are refused there,
and the one eventer call it runs is the replay's own. A replay is skipped when the client's own
passed list already carries the row, unless the host says the event is in flight; one whose world
or eventer is not up yet waits and is retried each second. A special fired before a peer joins is
not replayed to it: a scare box armed earlier stays unarmed on the joiner, and a decal it spawned
reaches the joiner only if a lane carries it. The developer menu's event trigger dispatches through the same host path, so a
forced event broadcasts like a scheduled one. It refuses running clients, including a join in
progress, and a queued trigger is bound to the submitting session's start serial and world
generation, so restarting the same session object cannot carry an old request into the next run.

The game keeps a registry of in-flight events: a reference count and the live event actors,
maintained by the actors themselves. The host sees each event begin and end at `lib_C::setEvent`
(a script-body watch) and, at a joiner's ready edge, reads the game's own registry and sends one
snapshot entry per in-flight event so the joiner replays it from the start with the in-flight
override (`coop/world/event_active_sync`). A creature with no event row is not replayed: the host sends the
creature's count of running events whole, by element id, and a client applies it to its mirror's
registration in the event count (`coop/world/creature_event_relay`) [?]; the creatures whose event
is computed from the local camera, render state or player 0 (the killer wisp, the fossilhound, the antibreather, the egg) keep their own per viewer. A client's killer wisp mirror is parked with its actor tick off, so the 3 s loop its BeginPlay keeps for that never runs there; the relay runs the same check for it every 3 s (this player within 50 m and not avoided) (VOTV-MP/Multivoid#193). Events whose only trace is a one-shot particle emitter
(the meteor shower) are mirrored as cues: the host sees the row's `runEvent` return and broadcasts
the emitter's identity and position, a joiner's snapshot reads the live emitters from the object
index, and a client spawns the same emitter (`coop/world/event_cue_sync`). A cue no row announces is taken
at its emitter's birth: the host sees the particle component born, reads its template at the next tick and
sends the component's own place (the growth area's glow at the plant it fertilizes) `[?]`.

An event whose actor stands on the host alone never registers on a client, and the game's `activeEvents` count is what
its threat music, the meta paranoia, plays on: while any event is registered, `mainGamemode.threatMusic` fades the threat
ambience in. The host tells every client when the first of its live registrations of such a class begins and when the
last ends (`EventCue` 104, `x` 1 or 0), and a joiner while one runs; a client registers one stand-in event, its eventer
(which never registers itself), through the game's own `setEvent`, and ends it (`coop/world/host_event_presence`). The
classes are listed there as each is measured: the vent knocker so far. `[V]` (`coop/dev/week2_drill`, arm `knock`, red
`week2_red=no_host_event`: slot 1 counted one event more from the knocker's spawn to the host's end; before, none.)

### The actors an event flies in

The saucers, the mothership, the ariral ships, the sky objects, the jellyfish, the fire tank and
their kin are not characters, so the character lane cannot drive them. They have their own
mirror (`coop/world/world_actor_sync`): a second interceptor on the deferred-spawn call with a
name-matched allowlist disjoint from the character one, a full-rotation pose batch, a spawn
that carries scale and a class-interpreted birth blob (a coin is born with the points its
material comes from), a destroy broadcast, and a dead-retire in the host's pose walk for the
actors that destroy themselves at an event's end. A spawner that runs inside a Blueprint (the
pyramid's, the jellyfish path's) is caught at the native function seam and gated by the spawning
class. A mirror stops whatever its class simulates with (the coin's sphere, the jellyfish's
capsule), so the pose drive alone moves it `[V]`. A heading
that lives outside the actor's rotation streams as an auxiliary yaw.

The walking pyramid rides those rails for its spawn, pose and despawn, the creature lane for
its four wisps, and the event lane for its identity; its own lane owns the one axis nothing
generic carries, the brain: the four state-writing timers are cancelled on the mirror, the
gather choreography is relayed, and the wisp it chases is streamed by identity so the mirror
runs the native chase look (`coop/creatures/piramid_sync`).

The vent crawler (day 5) is a character, so the creature lane mirrors it and refuses its BeginPlay and box overlap
on a client. The crawl is the host's: the first body in the crawler's box, the host's player or a client's puppet,
starts it, and the crawl's pose, the signal room and balcony doors' moves and the balcony door's keypads reach a
client on their own lanes. What the crawler's graph plays itself does not, and it has no lane of its own: the two
vent steps, the steps along the crawl, the metal break and its burst, the hanging vent's swing to where it stays
frozen, and the signal room door's power, written as a field. The host watches the box overlap, whose first call is
the one the graph acts on, and sends one EventCue (id 102); the client plays all of it on the crawl's own schedule,
the timeline's events at the times and with the volume and pitch its own tracks give, read from the mirror's
timeline, and the swing through the engine's own transform math, so the vent stops where the host's does
(`coop/world/vent_crawler_sync`) `[V]` (`harness/autotest/autotest_vent_crawler.cpp`: a client a minute past its join
starts the crawl with its own player; its door closes and loses its power, 23 sounds and the burst play, and the
doors, keypads and vent end as the host's).

The Ariral ship that shows outside the signal lab on day 13 (the arirShip event) is not one of them either:
`arirShipAppear_C` is placed in the level, 270 m out, and the arirShip event's box runs its `appear(true)`, which fades
it in and lights its radar point; its fade's end mails the sixteen OUTSIDE emails. That box runs on the host alone (its
other spawns have no lane), so a client never saw the ship (Ghezra/Multivoid#39). The host's post watch on `appear` sends
`EventCue` 103 when it comes up, and again to a joiner while it is up; a client runs its own copy's `appear(true)`
(`coop/world/arir_ship_sync`). Each copy's own tick then fades its ship out once that copy's player has looked at it, as
the game does for whoever sees it. `[V]` (`coop/dev/event_probe`, `VOTVCOOP_EVENT_PROBE=arirShip`: the host's forced box
raised the client's ship in the same second.) A client's copy's fade end is refused, since the host's mails reach every
inbox and a client's own emails are never sent: before, a client's inbox held the sixteen twice (63 rows where the host
had 47) `[V]` (`VOTVCOOP_EVENT_PROBE_EMAILS=1`).

The Antibreather's cave is shut by its spawner's rock (`antibreatherSPawner_C`'s `blockRock`, shown with its collision
on). The spawner's `block` timer, every 5 s, hides the rock while its Antibreather lives and shows it again once player 0
is past 50 m, and its BeginPlay hides it for a player starting inside; each copy ran that on its own player and its own
spawn rolls, so the cave stood open for a client and shut for the host (Ghezra/Multivoid#39). The rock is the host's: a
client's `block` is refused, the host sends the rock's state when it changes and to a joiner (`CaveRockState`), and a
client shows or hides its rock, collision with it. A host `block` that shuts the cave while a client's player is within
50 m of the spawner is undone, so a player near it is not shut out (`coop/world/cave_rock_sync`). `[V]` (`coop/dev/event_probe`:
the host's own spawner opened its cave at night, and a client that joined after found its shut cave opened as the host's;
the keep-open for a near client is `[?]`.)

The picnic's two Ariral warp arrows (day 8, `arirShipVanishing_C`) are not world actors either: the picnic is a
`trigger_spawnProp_C`, which spawns its prop row's class, and an arrow is a saved actor that stands where it is put and
fades for whoever comes near. It ran on the host alone, so a client saw only arrows its save already held, and kept them
after the host's picnic was cleared. An arrow now crosses by its place on the fixture lane, as a dirt hole does, and its
end crosses with it: a client binds a loaded arrow that stands where the host's does, spawns a copy only when none is, and
the joiner's census-end ends the loaded arrows nothing named (`coop/world/ambient_fixture_sync`). Each copy fades for its
own player. `[V]` (`coop/dev/week2_drill`, arm `ships`, with the rig save's leftover arrows cleared first: the host's
picnic took every peer from 0 arrows to 2, a joiner after it had the same 2, and the clearing took all three to 0;
before, a client saw none of the picnic's.)

The ship beside the radio tower's top from day 20 is another: `arirShip_tower_C` is placed there, and its begin play
loops every 5 s. A shown ship fades out (its fade's end leaves a `ufoDreamer_C` and ends it), and a hidden one fades in on
a 10% roll from 21:00 to 03:00 while player 0 stands in its box and has not just seen it; a radial force then pushes the
props near it. Every peer rolled it for its own player, so a client's ship showed on that client alone and pushed its
own copies of the props. The ship is the host's (`coop/world/arir_tower_ship_sync`): the host sends its `appear` as it
changes the ship (`EventCue` 106), and to a joiner while it is up, and every 5 s it rolls the game's own odds for each
client's player in the box who is not looking at it; a client's own `appear` is refused, and the host's runs on its copy.
`[V]` (`coop/dev/week3_drill`, arm `tower`, red `week3_red=tower_open`: with slot 1's player in the box, the host's roll
showed the ship on all three peers and took it away from all three, and slot 1's own `appear` was refused; before, slot
1's own showed on slot 1 alone.)

The lantern prank (day 15, `arirBuster`) leaves two crates and a notepad (`prop_notebook_busterSpawner_C`) at the
buildings by the Hole; they are props, and cross as any prop. Any use of the notepad swaps it for a plain notebook and
sends an Ariral at the door (`arirSlapper_C`, a follower child). A client's use ran on its own copy: the notepad went
for everyone through the destroy seam, and the Ariral stood on that client alone. A client's use is now asked of the
host, which runs it on its own copy as it converts a raw patty (`coop/props/prop_act_host`), and the Ariral the host's
notepad mints is caught at its spawn and mirrored (`coop/creatures/npc_world_enum`). `[V]` (`coop/dev/week3_drill`, arm
`lantern`, red `week3_red=note_local`: slot 1's use was granted and converted on the host, and the Ariral stood on all
three peers; before, on slot 1 alone, with the notepad gone everywhere.)

The warning obelisk (day 25, `obelisk_C`) is spawned by its event on the host alone. Its begin play drops it from high
above; its landing shakes every camera near it, fires its particles, flags its radar point (the base alarm), pushes the
props round it, flings seventy rocks, leaves the standing `prop_obelisk_C`, punches the main player and hides its own
mesh. The rocks and the standing obelisk are host births and crossed as props, so a client saw them appear with no fall,
shake or punch. The host now sends the obelisk's begin play while it has not fallen (`EventCue` 108 its yaw, then 107
its place), and a client spawns its own copy there, turned as the host's, so the fall and landing play for its own
player as the game's do: its own camera, its own punch, its own event count. The copy's push is turned off, as the
host's push moves the props, and the props the copy's landing makes are ended at their birth, as the host's cross
(`coop/world/obelisk_sync`). A joiner loads the obelisk the save holds. `[V]` (`coop/dev/week4_drill`, arm `obelisk`,
red `week4_red=obelisk_host`: slot 1's copy fell from 10 km to the ground at the host's yaw, its landing's 71 props were
ended, and every peer held the host's 136 rocks and one standing obelisk; before, slot 1 had no obelisk.)

A story event's trigger box (`trigger_box_N_C`) runs its targets when a player of its class list walks in, and the
host's copy sees a client's puppet, a `mainPlayer_C`, as it sees its own player: a client standing in the obelisk's box
ran the host's box and the obelisk fell on every peer `[V]` (`coop/dev/week4_drill`, arm `box`). A box whose row is
replayed on clients is armed there too, and the dropped Ariral gun's (day 24, `TB_event_arirToe`) then ran on the
client's copy as well, leaving that client a gun of its own beside the host's. A client's own copy of a box listed in
`coop/world/event_box_authority` is refused, so only the host's runs. `[V]` (arm `gun`, red `week4_red=box_local`: slot
1's walk-in into its own garage box left it a gun nobody else had; after, its box was refused and it had none.)

The Rozital scouts (day 32, `tentacleBall_C`, five from `tentacleBallsFollower_C`) run on the host alone, and a client
holds mirrors. A hostile ball traces from its light to each body within 150 m and, at a player, chases player 0 and calms
its buddies; within 40 m it pins the first player its sphere finds, once player 0's camera has open sky above it: that
player's `firstPersonBodyAnimation("pindown")`, and the ball lunges half way to player 0. A client's puppet on the host
was seen, and the chase went to the host; a pinned puppet played the pin on the host's copy alone. Each hostile ball still
looking is now served the nearest player (`coop/creatures/served_player`, its `GetPlayerPawn(0)`), kept while it chases
and pins, so its chase, its sky checks and its lunge go to that player; its camera read answers the same body. A pin of
a client's puppet is refused on the host and sent to that client (`EventCue` 109), whose own player plays it
(`coop/creatures/scout_target`). `[V]` (`coop/dev/week5_drill`, arm `scouts`, red `week5_red=scouts_host`: slot 1 15 m
from the scouts was seen, and before the chase walked 470 m to the host and pinned nobody; after, the balls hunted slot 1,
one pinned it, and its own player played the pin for 762 frames.)

The garage Soltomia (day 34, `soltomiaCleaning_C`) is spawned in the garage by its event on the host alone, hidden; its
begin play cleans the ATV. A player in its box shows it, pulls the ATV into the garage, shuts the garage door and unjams
the base's doors; the door opened, it plays its montage, whose throw hides its mesh and throws a `prop_sponge_solto_C`,
and it ends at the montage's end. The ATV, the doors and the sponge reach a client through their own lanes; the
Soltomia did not, so a client saw its ATV move and a sponge appear from nothing. The host sends its box's run once it is
shown (`EventCue` 111 its yaw, then 110 its place) and its `opened` (112). A client spawns a copy there, its begin play,
box and end refused, and shows it as the box's run does (its mesh, squeak, dust and talk); at 112 the copy plays the
montage, and the sponge its throw makes is ended at its birth, since the host's crosses (`coop/world/soltomia_sync`). A
joiner after it left gets the sponge. `[V]` (`coop/dev/week5_drill`, arm `solto`, red `week5_red=solto_host`: before, the
host's Soltomia showed and threw and slot 1 had none; after, slot 1's copy showed at the host's place and yaw, played the
throw, its own sponge was ended, and it ended with the host's one sponge.)

The sixth week's flyovers, the peeping UFO (`morningUfo_C`), the Rozital mothership (`rozitBorg_C`) and the drop
saucers (`ufoDropper_body_C`, `ufoDropper_car_C`), are spawned by the dispatcher on the host alone, and reached no
client. They are now among its mirrored outputs, and `coop/world/flyover_sync` carries what the actor pose does not.
The mothership's copy runs its own tick, which flies its ships, and takes the host's `alpha` every 2 s (`EventCue` 115).
The UFO's box is refused on a client, and the host sends its show (113); the UFO's look at player 0 is served the
nearest player, so a client's player can scare it off. A saucer copy's begin play, box, deploy, drop and end are refused;
at the host's deploy (114) it shows and plays its timeline, which lowers it and blinks this machine's lights, and the
car or body it drops is the host's. A client ends a saucer it loaded from its save where the host's copy stands or the
host has none (116: the host's saucers, sent to a joiner at its world-ready, then an end marker). `[V]` (`coop/dev/week6_drill`, red `week6_red=flyover_host`: before, slot 1 had none of the four;
with the lane off, its mothership stood at alpha 0 and its saucers never showed; after, its mothership tracked the host's
alpha, its UFO showed and flew, its saucers showed and blinked its lights, and the host's car reached it.)

### The ambient UFOs

The gamemode arms five timers at begin play, each rolling and re-arming itself: the ball (high over player 0's
camera, every 40 to 75 minutes), the boo (somewhere over the map at night from day 21, as often), the pill (over the
lake glow at night from day 7, every 90 to 112 minutes), Joel (over the radio tower by day from day 14 while a player
stands at its top, checked every 5 to 10 minutes) and Midas (the level's own UFO, at night from day 14, every 100 to
120 minutes); the last three come once a session. They are the host's, and every player sees the same ones. A client refuses all five timers (`coop/world/spawn_authority`). The host's four minted UFOs are
outputs of the gamemode's source on the world-actor lane, so each client gets a copy, which runs the UFO's own
animation. The ball and the boo roll their path on each machine at birth: the ball the turn and the two ends its
lights fly between, kept from its construction script because the engine refuses to move those parts afterwards,
and the boo the wobble of each point of its flight spline. So the host sends what its copy rolled (`UfoEvent`),
right after the spawn it follows on the same ordered lane (a joiner gets each UFO still flying, enrolled again if the last client's leave drained it, and its path at its ready
edge), and the client writes it over its own before the copy flies; a path the receive boundary refuses is logged with
the UFO's id, and that copy flies its own roll (a boo) or stays parked (a ball). The host relays the Midas take-off, and each client runs
its own. A copy the host ends first gives back the event entry its script would have given back on its own way out
(`coop/world/ufo_sync`) `[?]` (`harness/autotest/autotest_ufo.cpp`, `ufo_drill`: the host's ball path reaches a client's copy, and a
joiner's at its ready edge; the boo's path sync is `[?]`, the gamemode's boo needing day 21 and a night hour, which the drill cannot set; first rig run pending).

### The stolen radio tower

Every hour the gamemode's `ticker_radiotowerPoof` may (chance 0.005) spawn a `radiotowerPoof_C`. After a delay it checks
each second whether its peer's view has rendered the radio tower lately; once it has not, it hides the tower through
the tower's own `setHidden` and takes the collision of the tower, its ladder and its two segments away, and later, again
unseen, gives them back and goes. It ran on the one machine, so one player saw the tower gone with only its repair box
in the air, and the other saw it stand (VOTV-MP/Multivoid issue 63). The stolen tower is the host's: a client refuses its
own ticker at the script-body gate, the host's radio tower row on the fuse tower lane carries the hidden state
(`kFuseTowerStolen`), and a client whose tower differs makes the event's own calls (`coop/world/fuse_tower_ops`,
`coop/world/fuse_tower_kind`, `ue_wrap/devices/radio_tower`) `[V]` (`coop/dev/tower_poof_probe`, `VOTVCOOP_TOWER_POOF_PROBE=1`:
before, the host's tower went for 40 s and the client's stood; after, the client's went and came back in the same
second as the host's). A joiner gets the host's rows at its ready edge.

### The base alarm

One trigger actor is the base alarm: the siren, every red beacon, the ceiling flicker, the
basement grate and the in-flight registry. It is lit when the radar sweep hits an important
point (that is how an event lights it) or by the joke food, and stopped only from the radar
panel. The verb is idempotent and invisible, so both peers poll the active flag once a second:
the host broadcasts a transition, and a client that transitions first (its own sweep tripped,
its player pressed stop) sends its transition for the host to canonicalise
(`coop/world/alarm_sync`). The radar terminal's own beeps are per-viewer by design and have no
lane.

### The basement flood

A burst pipe (`burstWaterPipe_C`, placed in the basement) keeps one bit, `damaged`: its `State` turns its spray, sound and
tick on, its tick raises the basement's flood volume (`waterVolume_basementFlooder_C`, whose height between its two ends
is the level its `getLevel`/`setLevel` read and write), and a toolbox's `toolboxFix` mends it. Each copy ran it alone, so
a flood never reached a client and a client's repair never reached the host (Ghezra/Multivoid#40). The host owns both: the
host sends a pipe whose bit changed from the pipe's own `State` call and the level from the volume's own `addWater` and
`setLevel` once it has moved a whole unit. A client refuses three calls outside the lane's own apply: a pipe's `addDamage`
(a client's hit on a pipe is dropped, not forwarded: a burst is the host's) and the volume's `addWater` and `setLevel`
(so its level is only the host's). What still runs there is a burst pipe's `State`, spray, sound and tick (the tick's one
act is the refused `addWater`), the drain's tick and the volume's `updateWaterData`. The client applies the host's pipe
through `State` and the host's level through `setLevel` and `updateWaterData`, and refuses a level outside the volume's
range, 0 to its `height` (`coop/world/flood_sync`).

A client's repair runs on its copy and is asked of the host (`FloodIntent`), which trusts nothing in the ask but the
pipe's name: it mends its pipe only for a sender that is alive, within 400 units of the pipe by the host's own copy of
the sender, holds a toolbox by the hand the sender announced, and finds the pipe burst. An ask so judged and refused is
answered with the pipe's state, and a refusal at the target check leaves the pipe burst; an ask from a slot that is not
world-ready, past four a second per sender, or naming no pipe or another op is dropped with no answer.
The flood drill (`coop/dev/flood_drill`: `rig: VERDICT flood-burst`, `-mend`, `-far`, `-join`, `-mendold`) reads each of
these on the peers' own copies, and its red arms withhold the send, the judge and the joiner's replay `[?]`.

### The Ariral's float

The Ariral's float (`trigger_agrav_C`, the `agrav` event) runs on the host alone: five seconds in, the trigger gathers
the physics bodies within 100 m of the host's camera into `comps`, shuffles a copy, turns each one's gravity off and
sets it rising at 10 cm/s (added to its own velocity) with a random spin, and after its time turns each back on, one a
tick. A client ran none of it, so only the host saw the room float. The host now watches each body in a live trigger's
`comps` that simulates (a frozen prop does not move), five times a second while any waits or is lifted and once a second
otherwise: one whose gravity went off and that it then sees rise 3 cm is sent as an `AgravState` lifted row with the
speed it rose at, and one sent whose gravity comes back on as a dropped row, by host element id; a body that never rises
(blocked, or asleep) is not sent. A client switches its own copy's gravity the same way and sets it rising at the host's
speed, and once a second sets a lifted copy rising again when it has stopped and no drive row holds it (a host coast of
a body the float knocked ends with its copy asleep where the host's was); a dropped copy, once it rests, asks the host
where its prop rests (`coop/props/prop_rest_sync`), so every copy comes down where the host's did, and the lifted row
carries the body's spin, which a copy is set to (`coop/world/agrav_sync`; `coop/dev/event_probe` with `agrav`, and
`VOTVCOOP_AGRAV_TRACE=1` for each body's height at its lift and drop on every peer).

### The Arirals' shrimp

From day 11 the Arirals eat a shrimp pack left out: `shrimpEaterMaster_C`'s `eat`, every 15 s, takes the shrimp nearest
the last one it ate and, unless player 0 is within 100 m of that spot or the shrimp was rendered in the last 5 s, plays
the eat sound, leaves an empty pack and destroys the shrimp (a loyal reputation stops it). Every peer ran its own against
its own player: a client's eat destroyed the shrimp for everyone through the prop destroy seam, its empty pack stayed on
that client, and a shrimp beside a client was eaten by the host's. The eater is the host's
(`coop/props/shrimp_eater_authority`): a client's `eat` is refused, and the host's is refused as well for a shrimp a client's player is
within 100 m of the spot, or had in view in the last 5 s (its streamed eye and aim, read twice a second); the host's own
player stays the game's check. `[V]` (`coop/dev/week2_drill`, arm `shrimp`, red `week2_red=shrimp_open`: a shrimp beside
slot 1 was eaten within 15 s before and kept after; one 150 m from every player was eaten once, by the host, and all three
peers ended on the same 2 shrimps and 9 empty packs, where before they ended on 10 and 11).

### The Arirals' reputation

The reputation is one float in the save slot, `arirReputation`, and lib_C's `addAriralRep` (clamped) is its only writer.
Its callers are the world's: the treehouse's anti-steal box and its surroundings check, a graffiti's end, a fire tank's, a
gray boar's and theEvil's deaths, the follower's pickups, the ship and float photos, the player's wake. Every peer ran
them against its own copy, so each machine drifted to its own value, and what a client read from it (a brownie trail's
note, the eaters' tier) was its own. The value is the host's (`coop/world/arir_rep_sync`): the host sends it (`EventCue`
105, the value in `x`) when an `addAriralRep` moves it, once a second when another route moved it, and to a joiner; a
client's own `addAriralRep` is refused, and the host's value is written in. What a client's own player does is asked of
the host (`ArirRepIntent`), which makes it as its own: its wake (the gamemode's `wakeup`, five either way), its swing at
the follower (two off), and its photo of the visit ship (fifteen), which a client's copy now binds by raising the ship
through the ship's own `runTrigger`. The host judges each by its cause's own amount, one a second, and pays the ship's
photo once, whoever took it first, as the game's do-once does. `[V]` (`coop/dev/week2_drill`, arm `rep`, red
`week2_red=rep_open`: the host's +7 reached every peer and a joiner, and slot 1's own +100 was refused; before, the host
held 7 and slot 1 100. Slot 1's wake took every peer to 11.13, the game's own scaling of +5, its photo to 22.38, and its
second photo was refused as paid.) The swing is `[?]`: the drill's world held no follower.

### Sounds played at a place

`PlaySoundAtLocation` is a local one-shot, so a sound a Blueprint plays at a place on one machine is heard there alone:
a player's melee hit (`mainPlayer_C::attack`, the struck material's impact sound) and the vent knocker's knocks
(`kocker_C::knock`, an event actor on the host alone) reached no other player (Ghezra/Multivoid#41, #39). A pre hook on
that call sends the sound when the calling function is on the lane's list: a player's own, from the machine whose
player ran it, or the host's world, from the host (`SoundAt`). A sound crosses as an index into a table every peer
builds the same way, never as a path: the distinct impact sounds of the game's `list_physSound` table (34 on this build),
the knocker's knock, the church's bell and the cleaning sounds, sorted by path (`coop/world/sound_catalog`), read through
a verified walk of the table (`ue_wrap/world/physsound_table`). Each entry carries the callers that may play it, its attenuation and the
volume and pitch its callers pass, so the wire holds the index, the place, the volume and pitch (clamped to the entry's
range) and the sender's table digest; a receiver whose own digest differs drops the sound `[?]`. A table that cannot be
read yet is tried again at the next need, and until it builds a peer sends nothing and drops what it receives.

The cleaning sounds are two more callers on that table. A scrubbed grime decal's squelch (`grime_C::clean`'s `poop_Cue`)
is sent by the machine whose player cleaned it, since another peer takes the clean's dirt from the grime lane and never
runs `clean`. A cleaning item's use sounds are `PlaySound2D`, heard by its own player alone: the mop's and the sponge's
dip, the bucket's scoop, its water thrown out and its fuel poured (`wade_Cue`, `water_splash_Cue`, `poop_Cue`, `fuel1`,
the last found among the loaded sounds by its name). A second pre hook sends one the item in this player's hand played,
placed at the player with the game's `att_default`. `[V]` the squelch (`coop/dev/cleansound_drill`: the host's five
strokes of a decal played on slot 1 five times; before, none). The dip is `[?]`: the drill's sponge never dipped.

A client's sound is an ask, not a state: the host judges it (`coop/world/sound_judge`) before any peer plays it. The
entry must be a player's own sound, its melee hit, its scrub or its cleaning item's use (a knock or a bell is refused
whatever the client names), the sender needs a body
on the host within a melee swing's reach of the place named (500 cm and the authority's allowance for the puppet's
lag), and each client holds a budget of eight asks refilled at four a second. Each refusal is counted by reason. The
host then plays the sound and sends it to every client but the asker; its own sounds go to every client. A client's own
copy of the knocker or the church plays nothing, so the host's sound is the only one it hears, and its relayed play has no
Blueprint caller, so nothing echoes `[?]`. The wandering church (`church_C`) is on the list as the host's world: its tick
moves it, unseen, to a random place on the navigation mesh and rings its bell there. A client's puppet stepping
into the knocker's box, a metre cube in front of the main computer, starts the knocks on the host three seconds
later, the knocker's own delay `[V]` (`coop/dev/week2_drill`, arm `knock`). A sound is a one-shot, so a joining peer has nothing to be given: a
sound it missed is gone, as it is for a player who was not near. The drill is `coop/dev/sound_drill` (`sound_drill`: a
crowbar hit, the knock, and a far, a church-bell and a past-the-table sound the host must refuse); its rig proof is owed
`[?]`.

### Emails and the daily task

Every email producer funnels through one gamemode function into the save's list; the host owns
the append, and a receiver reproduces the whole thing in one reflected call, the row, the ding
at the laptop and the tab highlight, re-stamping the date from the synced clock; a deletion is
mirrored from either side (`coop/world/email_sync`). The daily task is host-authored, because
every live writer of it runs only on the host: the task creation is part of the midnight a
client's clock never reaches, and the drone sale is suppressed on clients; the host polls a change hash and sends the
task state, and sends it whole to a joiner at its world-ready, since a change during its load
was hashed but never reached it (`coop/world/daily_task_sync`); a joiner the task cannot be sent to
yet is owed it and the host retries each second. A receiver that cannot apply a task yet keeps the
latest one and retries it each second, and drops it if its world changes first; the writes set the
task's state and pay no reward. The rewards land in the shared balance, on
[base-computer.md](base-computer.md).

### Plants

A `growingPlant_C`, a player's planted seed or a winter bush alike, is a saved actor that a tick grows on its water, sun and rain. The
host runs every plant and sends `PlantState` with its row, place and values: at once on an edge a viewer sees, such as ripening or
death, and about every 5 s while it only changes slowly (`coop/world/plant_sync`). A client's copy does not tick: its tick, the herb's
`grown` and a sprinkler's watering are refused at the script gate (`coop/world/plant_gate`), and so are the spawners that plant
(`coop/world/spawn_authority`: the winter bush, the mushroom spore, the church's herb), so any change on it was that client's
player, and it goes to the host as `PlantEdit`, naming only the values that changed. The host judges each edit before it
applies it (`coop/world/plant_edit_gate`): the key, the row and class, the sender's reach, the pot a planting names and the
bounds of the values, at a bounded rate per client; it answers a refused edit of a plant it holds with that plant's state. A
plant is found by its save key, so what a join's load brought is already the host's, and a client's edits wait until its join has settled. A client's
harvest leaves the host's plant empty and the item with the harvester. A ripe crop's harvest (a plant whose row drops a
crop and is no bush or tree) drops its yield into the world and ends the plant, and a prop born that way on a client
reaches no other peer: the client's crops gave their yield to that client's screen alone. So a client refuses the game's
harvest of a ripe crop (a watch on `actionOptionIndex`, action 4) and sends the harvest as an edit
(`plant_edit::kHarvest`); the host judges it as any edit, checks that its plant is a ripe crop, and runs the plant's own
harvest, so the yield is the host's births on every peer and the plant's end reaches every client. A bush's berries go to
the harvester's inventory, and a discard or a tree's no-op changes only the plant: those still run on the client and reach
the host as edits. `[V]` on the rig (`harness/autotest/autotest_farm_harvest.cpp`): each list_farmPlants row planted ripe
and taken by each peer; a client's crop yield is seen on the host as the harvester counted it. A harvest's sound is the harvester's own game's, and every
other peer plays the plant's own `harvestSound`: a bush's berries going to none, or a grown plant whose harvest takes it whole
ending. Tests:
`harness/autotest/autotest_plant_state.cpp`, `harness/autotest/autotest_farm_harvest.cpp`.

## Who owns what

| State | Owner | Shape |
|---|---|---|
| the clock, the sky | the host | streamed; the client's clock held at zero rate |
| rain, snow, fog, wind, lightning | the host | scheduler observed on the host, cancelled on the client; the client's births killed |
| the red sky | the host | its toggle sent from the host as it returns, refused on the client |
| the Bad Sun's existence | the host | its spawn and its copy's `remove()` sent as they return, a client's own spawn refused; each peer's copy runs by the shared clock and its harm is local |
| fireflies | each peer | peer-symmetric union |
| ambient spawners | the host | refused on clients at the script-body gate; the camera-anchored ones per peer |
| a scheduled or story event | the host | observed at its verb; replayed on the client per row |
| the in-flight registry | the game, on each peer | mirrored to a joiner as a snapshot |
| an event's actors | the host | the world-actor mirror |
| the stolen radio tower | the host | its ticker refused on a client; the hidden state on the radio tower's fuse tower row |
| the ambient UFOs | the host | its timers refused on a client; its UFOs on the world-actor mirror with the host's rolled path; the Midas run relayed |
| the pyramid's brain | the host | timers cancelled on the mirror; the gather relayed |
| the base alarm | whoever transitions | polled; the host canonicalises |
| the basement's pipes and flood | the host | a client's `addDamage`, `addWater` and `setLevel` refused outside the lane's apply; the bit and the level sent as they change; a client's repair asked |
| the radio tower's Ariral ship | the host | its `appear` sent and rolled for every player in its box; a client's own refused |
| the Rozital scouts' hunt and pin | the host | each hostile ball serves the nearest player; a client's pin is sent to it |
| the garage Soltomia | the host | its show and its opened sent; a client's copy shows and throws, its gameplay refused |
| the sixth week's flyovers | the host | mirrored; the mothership's alpha, the UFO's show and the saucer's deploy sent |
| the Hole Repair | the host | its controller's box refused on a client; its room's place sent; its guards and Igetis mirrored |
| the warning obelisk's fall | the host | its begin play sent; a client's own copy falls and lands, its push off and its prop births ended |
| a story box whose walk-in makes shared props | the host | the host's box sees a client's puppet; a client's own copy of a listed box refused |
| the lantern prank's notepad | the host | a client's use asked and run on the host's copy; the Ariral it sends mirrored |
| the picnic's warp arrows | the host | each by its place and its end, on the fixture lane; a loaded one bound by place |
| the Arirals' shrimp eater | the host | a client's `eat` refused; the host's refused near or in view of a client's player |
| the Arirals' reputation | the host | sent as it moves and to a joiner; a client's own `addAriralRep` refused |
| a host-only event's count | the host | its first begin and last end sent; a client registers one stand-in event |
| the Ariral's float | the host | the bodies its trigger lifts and drops sent as each switches; a client's copy switched the same way and put where the host's rests |
| emails, the daily task | the host | one reflected append; a change-hash poll |

## Wire messages

| Kind | Direction | Carries |
|---|---|---|
| `ClockPose` (stream), `SkyState` | the host to all | the clock and the day number; the dome, the moon, the eye and the black hole |
| `WeatherState`, `LightningStrike`, `RedSky`, `BadSun` | the host to all | the state fields; a strike location; the red sky's edge; whether a Bad Sun exists |
| `FireflySpawn` | each peer, relayed | one spawn |
| `EventFire`, `EventSnapshot`, `EventCue` | the host to all; the host to one joiner; the host to all | a fired row; an in-flight event; an emitter cue |
| `CaveRockState` | the host to all | the Antibreather cave's entrance rock, shut or open |
| `WorldActorSpawn`, `WorldActorDestroy`, `WorldActorPose` (stream) | the host to all | an event actor's birth with scale and blob; its death; full-rotation poses |
| `PyramidGather` | the host to all | the gather commit |
| `UfoEvent` | the host to all; the host to one joiner | a UFO's rolled path (a ball's turn and ends, a boo's flight spline), or the Midas take-off |
| `AlarmState` | either role | the alarm's active flag |
| `EmailAppend`, `EmailDelete`, `TaskNewState` | the host to all; either role; the host to all | an email; a deletion; the task |
| `FloodState`, `FloodIntent` | the host to all; a client to the host | a pipe's `damaged` or the flood's level; a client's repair |
| `AgravState` | the host to all | the props the Ariral's float lifted or dropped, by host element id |
| `ArirRepIntent` | a client to the host | a change to the Arirals' reputation its own player made: the amount and its cause |
| `SoundAt` | either role; the host judges a client's and sends it to the other clients | a sound's index in the sound table, place, volume and pitch, and the sender's table digest |

## Late join

The clock streams from the connect, so a joiner's first sample once its world exists sets its time
and day number; the sky (its eye included), the weather state, the red sky's state, red or
clear, and whether a Bad Sun exists are seeded at the joiner's world-ready; the join blob already
carries a saved Bad Sun copy, and the seed settles it against the host's state.
The in-flight events arrive as snapshot entries and replay with the override; the live cues are
re-sent; every tracked event actor is re-spawned on the joiner, an ambient UFO with its rolled path, and the Midas UFO's
run is re-sent while it flies; the pyramid's gather in flight
is re-sent after its mirrors exist; the alarm's current flag is sent unconditionally, and the
joiner runs the native trigger, which is idempotent against a save that already carried it.
Emails are seeded as a delta against the blob instant; the task state is sent at the ready
edge. A one-shot cue a joiner was not present for is missed, by definition. The basement's flood is not one: every pipe's
bit and the flood's level are sent to the joiner at its ready edge, and the host's changes follow `[?]`
(`coop/dev/flood_drill`, arm `join`: a client that leaves a rising flood and comes back reads the host's pipes and level).

## Known limits

| Limit | Evidence |
|---|---|
| The sky's earth teleport plays its two sounds on every client (`coop/world/world_sound_sync`, EventCue ids from 100, found by the sound the host's `PlaySound2D` is given); its black hole and its light ride SkyState | `[?]` `harness/autotest/autotest_world_sound.cpp`: the cue received and the play call made on the client (the rewritten drill has not run yet); `[RD]` for the rest |
| A photo a client takes of the Ariral float earns no reputation: the float's trigger binds its `photoTaken` in its own `runTrigger`, which only the host's copy runs, and running it on a client would start the float there | `[RD]` `trigger_agrav_C.runTrigger` |
| The alien sounds are each player's own: the event is replayed, and each peer's three noisers walk around its own player, sound and leave when that player sees one. A player does not hear the noisers around another. A joiner whose world is up while the host's noisers are out is sent the event once at its ready edge (the join snapshot, one row for the three noisers) and gets noisers of its own | `[?]` `harness/autotest/autotest_world_sound.cpp`: three noisers on each peer, and on a rejoined client (the rewritten drill has not run yet) |
| A harvest's sound is the harvester's alone: no other peer plays it | `[RD]` the harvest half of #104 waits for the host to perform every harvest (#115): the host's own `growingPlant_C::harvestSound` call then sends an EventCue id 102 at the plant |
| A joiner who arrives during a black fog gets a whole one, from its fade-in | `[?]` `coop/world/weather_fog` |
| The eyes a black fog spawns on the host are not mirrored to clients | `[V]` `coop/dev/blackfog_probe` (host 4, client 0) |
| The flesh rain is rolled by every peer's own hour pulse and is not mirrored (the sky eye and the jellyfish are not: `SkyState` carries the eye, the world-actor lane the host's fish, and a client's own `setEye` and path spawn are refused) | `[RD]` the clock's hour roll; the jellyfish is on the world-actor mirror's list, but its spawn happens inside a Blueprint where the mirror's catch does not see it |
| A decorated Christmas tree spawns its gifts on each peer whose player sleeps through midnight, and a client's gifts are its own | `[RD]` the tree's own check of the local player's sleep at hour 0 |
| Several rolls are still per peer: the rare gamemode rolls (the one-percent forced quit), the server break-minigame variant, the underground loot mounds, the signal scramble and the radio-tower shuffle | `[V]` no lane under `coop/world` carries them; `coop/interactables/garbage_sync` names the mounds |
| The flora spawner is not refused on a client, so each peer rolls its own (the deer, walking trees, dirt holes and beehives are the host's, `coop/creatures/ambient_creature_sync` and `coop/world/ambient_fixture_sync`, and so are the walking mannequins, whose spawner is a row of `coop/world/spawn_authority`) | `[V]` no row of `coop/world/spawn_authority` names it; the table in `docs/npcs-and-kerfur.md`, "Which spawners a client refuses" |
| A plant runs on the host's sun, rain and view of the world alone: a client refuses the bodies that change a plant on their own (`coop/world/plant_gate`). A fruit a client's toolgun forces on a plant that bears fruit as props stays on that client, as the prop it spawns does not cross; the host grows its own at its next ripening. What a client's copy shows between two gradual sends can be up to 5 s old | `[V]` `coop/world/plant_sync`; `tool_plantFruit_C`; `[?]` `coop/world/plant_gate` |
| Rain falls inside the base for a player standing indoors, the host included: the game's own effect, the same in single player. The rain emitter follows the player's camera and spawns its drops around it, below the roof's rain-collision mesh, which stops only drops falling onto it from above | `[V]` `coop/world/weather_rain` never moves the emitter or the camera; `[RD]` the decompiled day-night cycle's emitter move |
| A client that joins during a vent crawl gets the crawler from the join census but not the crawl's sounds, the vent's swing or the door's power, which start from the crawl's one cue; the crawl was forced through the eventer's own verb, not reached by the day 5 schedule | `[?]` `coop/world/vent_crawler_sync` |
| The ball appears above the host's camera, and Joel's roll looks for a player at the radio tower's top on the host, so both follow the game's player 0, which is the host | `[RD]` the gamemode's `ufo_ball` and `ufo_joel` |
| A joiner's UFO copy starts its flight from the beginning, so it trails the host's by the time the UFO had flown | `[?]` `coop/world/ufo_sync` |
| Trigger-volume fires (a bed event, a scare a player walks into) run per peer, as the single-player design intends | `[V]` by design, not a gap |
| Only the pyramid and the alarm have had the one-event-at-a-time pass; every other event rides the generic lanes on the per-row replay policy | `[V]` `coop/world/event_fire_sync`, the replay allowlist |

## Code map

| Concept | Files |
|---|---|
| the clock and the sky | `coop/world/time_sync`, `coop/world/day_edge`, `coop/world/sky_sync`, `ue_wrap/world/daynightcycle`, `ue_wrap/world/profile`, `ue_wrap/world/event_list`, `ue_wrap/world/skysphere` |
| weather | `coop/world/weather_sync`, `coop/world/weather_rain`, `coop/world/weather_fog`, `coop/world/weather_lightning`, `coop/world/weather_redsky`, `coop/world/weather_badsun`, `coop/world/weather_event_births`, `ue_wrap/world/directionalwind`, `ue_wrap/world/bad_sun` |
| fireflies and the spawners | `coop/world/firefly_sync`, `coop/world/spawn_authority` |
| events | `coop/world/event_fire_sync`, `coop/world/event_active_sync`, `coop/world/creature_event_relay`, `coop/world/event_cue_sync`, `coop/world/vent_crawler_sync`, `coop/dev/event_trigger` |
| event actors and the pyramid | `coop/world/world_actor_sync`, `coop/world/world_actor_mirror`, `coop/creatures/piramid_sync` |
| the stolen radio tower | `coop/world/fuse_tower_ops` (the ticker's refusal), `coop/world/fuse_tower_kind`, `ue_wrap/devices/radio_tower`; `coop/dev/tower_poof_probe` |
| the ambient UFOs | `coop/world/ufo_sync`, `coop/world/spawn_authority` (the gamemode's five timers); `harness/autotest/autotest_ufo.cpp` (`ufo_drill`) |
| the alarm, emails, the task | `coop/world/alarm_sync`, `coop/world/email_sync`, `coop/world/daily_task_sync`, `ue_wrap/world/email` |
| the basement flood | `coop/world/flood_sync`; `coop/dev/flood_drill` |
| the Arirals' shrimp and reputation | `coop/props/shrimp_eater_authority`, `coop/world/arir_rep_sync`; `coop/dev/week2_drill` |
| the Rozital scouts | `coop/creatures/scout_target`, `coop/creatures/served_player`; `coop/dev/week5_drill` |
| the garage Soltomia | `coop/world/soltomia_sync`; `coop/dev/week5_drill` |
| the sixth week's flyovers | `coop/world/flyover_sync`, `coop/creatures/npc_world_enum`; `coop/dev/week6_drill` |
| the Hole Repair | `coop/world/hole_sync`, `coop/world/spawn_authority`; `docs/npcs-and-kerfur.md`; `coop/dev/week6_drill` |
| the warning obelisk, the garage box | `coop/world/obelisk_sync`, `coop/world/event_box_authority`; `coop/dev/week4_drill` |
| the radio tower's ship, the lantern prank | `coop/world/arir_tower_ship_sync`; `coop/props/prop_act_host`, `coop/props/prop_act_request`, `coop/creatures/npc_world_enum`; `coop/dev/week3_drill` |
| a host-only event's count | `coop/world/host_event_presence` (fed by `coop/world/event_active_sync`) |
| the Ariral's float | `coop/world/agrav_sync`, `ue_wrap/engine/engine_physics` (the gravity switch); `coop/dev/event_probe` |
| sounds played at a place | `coop/world/sound_relay`, `coop/world/sound_catalog`, `coop/world/sound_judge`, `ue_wrap/world/physsound_table`, `ue_wrap/core/object_path`; `coop/dev/sound_drill`, `coop/dev/cleansound_drill` |
| tests | `harness/autotest/autotest_weather.cpp`, `autotest_fog_probe.cpp`, `autotest_eventforce.cpp`, `autotest_alarmforce.cpp`, `autotest_piramidforce.cpp`, `autotest_vent_crawler.cpp`; the event drill, `coop/dev/event_drill`; the world roll drill, `coop/dev/world_roll_drill` |
