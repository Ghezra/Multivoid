# Kitchen and media

## Purpose

The microwave, the mini oven and the grill, the placed radios and TVs with the ATV's radio, the GPU mining rig and the
fire extinguisher's foam, and the 3D printer and what it prints: who owns each one's state and how a peer's press or pick reaches the others.
The other keyed devices around the base are in [devices.md](devices.md).

## How it works

### The kitchen's machines

A microwave's and a mini oven's presses run on the presser's copy, so each peer polls them for its player's change and
sends it (`KitchenState`); the others apply it through the machine's start or repaint, so the host's machine heats the
food it authors. Each peer's timer ends its own microwave's run, so what crosses is its start, its door and a stop before
its timer's end, through the game's own `forceOpenDoor`, `openDoor` and `closeDoor`. A joiner gets every machine
off its defaults, since an oven's controls are in no save `[V]` (`harness/autotest/autotest_cook_state.cpp`: each
control from each peer, a food heated in a microwave a client started, a shrimp's progress and its one cooked food, a
rejoin; machines powered by a write and pressed by reflection). A grill burns coals in its tick on every peer, so the
host's count stands: a client sends only its player's coal, light, off press or lid, and each host message sets the client's burn, so
its copy never runs out first; the drill runs each from each peer, a food heating on it, three coals burnt on the host's
count, the lid's own press and its return from each peer, and a lit grill for a rejoin, the verbs by reflection.

### Radios and TVs

A placed radio or TV (`coop/world/media_device_sync`) plays through each peer's own media player and picks its own next track, so the
host's state stands: a client's copy is moved to its track, time and play state, and a client with the radio's interface
open sends what its player changed. A track crosses as a hash of its catalog link and opens only from the peer's own
catalog `[V]` (`harness/autotest/autotest_radio_state.cpp`: a pick, stop, play and mode from a client, a pick from the
host and a rejoin, with silent test tracks, the rule written on and the pick by the radio's own function). A TV also
carries its channel, volume, brightness and squeeze; its openLink needs its power as well as the rule, so a track waits
for its cord `[V]` (`harness/autotest/autotest_tv_state.cpp`: the same from each peer with a channel and the picture, with
a silent audio-only `.mp4`, powered by a write, so no picture was decoded). The ATV's radio is one of them, a child actor
of the ATV named by the ATV's key (`atv:<key>`) `[?]` (a pick from each peer through the radio's own interface was read
off its media player on both, and on the code before it each pick stayed on its own peer, by the ATV host drill that
the rework of the ATV's host lane retired).

### The GPU mining rig

A mining rig (`prop_miner_C`) counts its slots, GPUs and coolers in its own fields: a held scrap metal, GPU or cooler used
on it runs its `upgrade`, which counts one more and repaints it, and the item goes. Its tick mines points while it is
powered and cools its heat, and its claim (action 4) pays the points into the balance. Each ran on the one machine, so a
rig one player filled stood empty for the other, and each copy mined its own points (VOTV-MP/Multivoid issue 43). The
rig is the host's: a client refuses its tick and its fire (`fireDamage`, which spawns its debris and loose GPUs, so
the host's burning rig is the one that throws them), and its use of an item and its claim are refused and asked of the host
(`MinerIntent`), which judges the reach (6 m) and pace and runs its own rig's `upgrade` or claim, the rig's hints going
to the asker. A granted use spends the item in the asker's hand. The host sends each rig's counts, points and heat when
they change, the points at most once a second, and to a joiner at its ready edge (`MinerState`); a client writes them and
repaints through the rig's own `upd` (`coop/props/miner_sync`) `[V]` (`coop/dev/miner_probe`, `VOTVCOOP_MINER_PROBE=1`:
before, the host's rig held 2 GPUs, 2 slots and a cooler and the client's copy none, and a client's added slot stayed on
the client; after, the client's copy took the host's counts in the same second, a client's scrap metal added a slot on
the host and on both copies, and its claim ran on the host, the host's hint shown on the client). The probe's use names
the item without one in the hand, so the spend of a real held item, and a powered rig's points, are not measured `[?]`.

### The fire extinguisher's foam

A fire extinguisher (`prop_fireExt_C`) sprays from a player's hand: its `fire`, the hand's LMB press and release, turns
its spray effect and sound on, and its tick aims the spray along that machine's camera, drains its charge and casts the
game's `extinguishRay`, which puts out what it touches. Hit hard or burnt in the world, its `shoot` sends it thrusting
with its own foam and rays until the charge is gone. Each ran on the one machine, so the other players saw no foam
(Ghezra/Multivoid issue 31). The foam is the host's: a player's spray is that player's to start and stop, and goes to
the host (`FireExtIntent`), which checks the sender's hand holds an extinguisher, shows the spray on its mirror of that
hand, casts the extinguishing ray along the sender's aim each frame while it runs, and tells every peer
(`FireExtState`), each showing it on its mirror of that hand. A client's own ray and its `shoot` are refused, so a fire
goes out, and an extinguisher thrusts, only on the host; the host sends each world extinguisher's thrust and charge when
they change and to a joiner, and a client's copy shows the thrust's foam and sound (`coop/props/fireext_sync`) `[V]`
(`coop/dev/foam_probe`, `VOTVCOOP_FOAM_PROBE=1`: a client's spray showed on the host's mirror within a second and the
host cast 364 rays along its aim; the host's spray showed on the client's mirror; a world extinguisher the host set
thrusting thrusted on the client's copy, its charge following the host's). A fire put out by a client's spray is not
measured `[?]`. The charge of an extinguisher in a client's hand is the host's count (Ghezra/Multivoid issue 34): the
spray's start and end carry the hand's charge, the host drains its own count at the game's rate (1/60 a second) while it
runs, never above the hand's word, and sends it twice a second and at the end; the sprayer's hand and every mirror of it
take it, so its drop carries the host's count `[V]` (`coop/dev/item_trip_probe`, `VOTVCOOP_ITEM_TRIP_ITEM=ext`: six
seconds' spray from 1.000 read 0.900 in the hand, on the host and on both copies after the drop, either player spraying).

### The 3D printer

The printer (`3dPrinterAnim_C`, a prop) reads a model from the player's own disc: the folder
`<asset folder>\meshes\printer\<name>`, its `<name>.obj` and what sits beside it, where the asset folder is the
gamemode's `assetFolder` or `%LOCALAPPDATA%\VotV\Assets`. At its print's end it spawns a `printedObject_C`, gives it the
model's folder name and hands it the mesh it loaded; the object's save record keeps the name, and its `load` reads the
model from that player's disc again, destroying the object with a hint when the file is missing. `printedObject_C` is
no `prop_C`, so no prop lane carried it: a print stood on its printer's machine alone, and a client saw it only after a
save brought it, never moving with the host's after that (VOTV-MP/Multivoid#49).

A print is the host's, and runs only when every connected player holds the same model (`coop/props/printer_sync`). A
model is named by a hash of its whole folder, each file's path inside it and its bytes (`coop/props/printer_models`).
A client's `print` is refused on its copy of the printer and asked of the host (`PrintAsk`), which judges the reach
(8 m), selects the model on its own printer and checks it; the host's own `print` is held for the same check while a
client is connected. The host asks every connected client for its hash (`PrintCheck`, `PrintCheckReply`) and waits up
to 5 s; it prints when every hash equals its own, and otherwise refuses: a chat line on the host, the asker and each
player lacking the model names who lacks it or holds another copy, and which (`PrintNotice`).

The host gives each printed object a keyless prop element, as the trash clump has, and mints its save key with the
object's own `GetKey`. The element lets the held-prop stream carry it while a player holds it, which needs nothing else
of a prop. The host sends the object (`PrintedState`): its element id, key, model and the model's hash, light and bites,
and where it rests, at its print, when its light or bites change, once it comes to rest moved (over 2 cm or 3
degrees), every one to a joiner, and its end. Between rests each peer's own physics carries it, as a thrown prop. A
client spawns it, or adopts the copy its save loaded by key, and builds it from its own file when its hash matches
the host's. A client without that copy keeps a placeholder, hidden, with no collision and no physics, so the object's
identity stays and nothing of another model stands in its place; it tells the host (`PrintedIntent`), which puts the
chat line on the host and that player once per model. The game's own `load` of a save-loaded printed object is refused
on a client without the model, which would otherwise destroy it. A client's light toggle is asked of the host; its bite
is its own player's food and is told to the host, which counts the bites and ends the object at the last.

The printer's run, the head tracing each layer, is its own tick, so no stream carries it: the host sends when a print
began and when it ended (`PrinterState`), and a client's copy of the printer runs the game's own `selectMesh` and
`print` from its player's file between the two. A copy whose run reaches its end spawns an object of its own; its
`loadFromPrinter`, called by the printer, is refused and the object destroyed at the next tick, as the host's arrives
as a printed object. A copy still running at the host's end is stopped as the run's own end stops it. A joiner gets
each printer printing.

`[V]` on the rig, three peers (`coop/dev/kprint_probe`): a client's print went through the host's check and printed on
the host; both clients built the object from their file at its element id; a lift on the host moved both clients'
copies to the host's rest, and its light reached both; a client joining after the print adopted its save-loaded copy
by key. With one client reading as lacking the model (`VOTVCOOP_PRINTER_MODEL_FAKE=missing`, the rig's copies sharing one
asset folder): joined before the print, the host refused it and the chat line stood on all three; joined after, the
game's load of its save-loaded copy was refused, the copy was adopted as a placeholder and the line stood on the host and
that client. A print left to its own length (45 s for a 20 cm cube): the asker's copy of the printer ran it, its height
within a step of the host's; at its end its own object was destroyed and the host's came, one object on each peer. Not
measured: a player's own hold and throw of a printed object, a client's light toggle and bite, a copy with another hash
(`differs`), and a joiner arriving mid-print.

## Wire messages

| Message | Direction | Carries |
|---|---|---|
| `KitchenState` | the presser to all; the host's burn of a grill's coals | a microwave's or a mini oven's setting and whether it runs; a grill's coals, fire, lid and burn |
| `MinerState`, `MinerIntent` | the host to all, an answer to its asker alone; a client to the host | a rig's GPUs, slots, coolers, points and heat; a client's use of an item or its claim |
| `FireExtState`, `FireExtIntent` | the host to all; a client to the host | a world extinguisher's thrust and charge, a player's spray; a client's spray on or off |
| `MediaDeviceState` | the host to all; a client to the host | a device's track (a hash of its catalog link), time, play state and mode, and a TV's channel and picture; a client's change through its interface |
| `PrintAsk`, `PrintCheck`, `PrintCheckReply`, `PrintNotice` | a client to the host; the host to each client; each client to the host; the host to a client | a client's print, by the printer's element id and the model; the host's question of a model; the client's hash of its folder, 0 for none; a chat line naming who lacks which model |
| `PrinterState` | the host to all | a printer by its element id began printing a model, or its print ended |
| `PrintedState`, `PrintedIntent` | the host to all; a client to the host | a printed object's element id, key, model and hash, light, bites and rest, or its end; a client's light toggle, its bite, or that it lacks the model |

## Late join

Every radio and TV again at the joiner's ready edge, every kitchen machine that is off its defaults, and every printed
object (a save-loaded copy adopted by its key).

## Known limits

| Limit | Evidence |
|---|---|
| A radio or TV plays only on a peer whose own catalog holds the track and whose Custom Content rule is on, and is silent elsewhere; a TV showing a security camera is not this lane's | `[V]` `coop/world/media_device_sync`: a track is a hash, so no peer can make another open an address |

## Code map

| What | Where |
|---|---|
| a microwave's, a mini oven's and a grill's controls | `coop/interactables/kitchen_sync`, `ue_wrap/devices/kitchen` |
| a placed radio or TV | `coop/world/media_device_sync`, `ue_wrap/world/media_device` |
| the GPU mining rig | `coop/props/miner_sync`; `coop/dev/miner_probe` |
| the fire extinguisher's foam | `coop/props/fireext_sync`; `coop/dev/foam_probe` |
| the 3D printer and its printed objects | `coop/props/printer_sync`, `coop/props/printer_models`; `coop/dev/kprint_probe` |
