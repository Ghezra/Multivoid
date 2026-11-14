# Kitchen and media

## Purpose

The microwave, the mini oven and the grill, the placed radios and TVs with the ATV's radio, and the GPU mining rig: who
owns each one's state and how a peer's press or pick reaches the others.
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
rig is the host's: a client refuses its tick, and its use of an item and its claim are refused and asked of the host
(`MinerIntent`), which judges the reach (6 m) and pace and runs its own rig's `upgrade` or claim, the rig's hints going
to the asker. A granted use spends the item in the asker's hand. The host sends each rig's counts, points and heat when
they change, the points at most once a second, and to a joiner at its ready edge (`MinerState`); a client writes them and
repaints through the rig's own `upd` (`coop/props/miner_sync`) `[V]` (`coop/dev/miner_probe`, `VOTVCOOP_MINER_PROBE=1`:
before, the host's rig held 2 GPUs, 2 slots and a cooler and the client's copy none, and a client's added slot stayed on
the client; after, the client's copy took the host's counts in the same second, a client's scrap metal added a slot on
the host and on both copies, and its claim ran on the host, the host's hint shown on the client). The probe's use names
the item without one in the hand, so the spend of a real held item, and a powered rig's points, are not measured `[?]`.

## Wire messages

| Message | Direction | Carries |
|---|---|---|
| `KitchenState` | the presser to all; the host's burn of a grill's coals | a microwave's or a mini oven's setting and whether it runs; a grill's coals, fire, lid and burn |
| `MinerState`, `MinerIntent` | the host to all, an answer to its asker alone; a client to the host | a rig's GPUs, slots, coolers, points and heat; a client's use of an item or its claim |
| `MediaDeviceState` | the host to all; a client to the host | a device's track (a hash of its catalog link), time, play state and mode, and a TV's channel and picture; a client's change through its interface |

## Late join

Every radio and TV again at the joiner's ready edge, and every kitchen machine that is off its defaults.

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
