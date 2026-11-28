# More events

The basement fridge's knocks, the Arirals' bomb box, an Ariral's punch on a door and the Ariral float's show. The other story events, the weather and the sounds played at a
place are on [events-and-weather.md](events-and-weather.md).

## What the game does

The basement fridge (`fridgeGlow_C`, placed in the level) knocks two ways, each from a timer of its own. Past day 14,
`timer_3knock` rolls each second: a 1% roll arms it while player 0 can see the fridge, and a second 1% roll runs
`startKnocking`, once a session (`knocked`). That plays three loud knocks, each with a world camera shake at the fridge
(`TentacBallStepShake_C`, 100 inner, 5000 outer), then shows the figure behind the door. Past day 7,
`timer_removeObscurity` runs `knockObscurity` when props pushed against the door have added to `knockamount` (or on a
10% roll): a quieter knock whose volume and pitch follow `knockamount`, pushing and damaging the props in front.

The Arirals' bomb box (`prop_bombbox_C`, a `trigger_eventer` gift) spawns an explosion at its `exp` point on action 4
and destroys itself.

The lantern prank's notepad spawns `arirSlapper_C`, an Ariral follower (`npc_arirFollower_C`) sent to a closed door. Its
ubergraph punches every door in reach: the door's `addDamage` (5), the struck material's impact sound and its laugh
(`outsideAlien_Cue`), both through `SpawnSoundAtLocation`; it also eats, swings, screams and slaps through
`PlaySoundAtLocation`. `door_C::addDamage` pushes the door's two leaves (`door_r_root`, `door_l_root`) part of the way
open by the damage, and opens the door once the right leaf is more than 90 cm from its open place.

The Ariral's float (`trigger_agrav_C`, spawned on the host alone; its bodies are on
[events-and-weather.md](events-and-weather.md)) copies its gather, `comps`, to `buf` and takes each body off `buf` as it
switches that body's gravity. Its show is on the trigger: `cloak(true)` plays a timeline that fades its shimmer in
(`dynmat_0`, `dynmat_1`, `pointlight1`), its `pointlight` is attached to player 0's camera and shown, and its
`arirhover_cue` hums; at the end the hum stops, the light is destroyed and `cloak(false)` fades the shimmer out.

## How it works

The fridge (`coop/world/fridge_knock_sync`): each player's fridge rolled its own dice against its own player, so a
client never heard the host's knocks nor felt the shake (Ghezra/Multivoid issue 61). A client's two timers are now
refused, and the host looks at its fridge every frame: `knocked` newly set is the big knock, sent as EventCue
133; `knockamount` spent to 0 is the quiet knock, sent as EventCue 134 with its sound's volume and pitch. A client runs
its own `startKnocking` for the big knock (the knocks, the shake on its own player, the figure) and plays its quiet
knock sound at the host's volume and pitch. The props the quiet knock pushes are the host's, on their own lane.

The bomb box (`coop/props/prop_act_request`, `prop_act_host`): a client's blast was its own world's, which the
explosion lane removes there, and no request reached the host, so it never exploded (issue 64). A client's action 4
on the box is now asked of the host as a conversion is: the host runs the box's own `actionOptionIndex`, its explosion
reaches every peer through the explosion lane, and its destroy removes the box everywhere.

The Ariral's punch (Ghezra/Multivoid issue 63): the follower stands on every peer through the npc lane, and a client's
copy ran its own ubergraph, whose sounds came from its own copy's moves, while the host's door was hit and a client's
was not, so its leaves stood shut there. Now the follower's ubergraph is a host-world caller of the sound relay
(`coop/world/sound_relay`, bit `kArir` in `coop/world/sound_catalog`): the host sends its sounds as `SoundAt`, and a
client's own are refused. `SpawnSoundAtLocation` returns the AudioComponent it plays through, so it is watched after the
call: the host reads the sound, the place, the volume and the pitch off that component, and a client stops its own.
After every `addDamage` on a door the door lane indexes, the host sends the two leaves' places when they moved
(`coop/interactables/door_dent_sync`, `DoorDentState`); a client writes them unless its copy is swinging, where the
door's own open state ends the swing where the host's does.

The float (`coop/world/agrav_sync`, issue 59): the host scanned `buf`, which is empty a moment after each switch, so at
most the body of that moment was sent; it scans `comps` now. A client has no trigger, so it saw no shimmer, light or
hum. The host watches the trigger's `cloak` and sends its begin as EventCue 135 at the trigger and its end as 136; a
client spawns its own copy of the trigger there, which never runs its `runTrigger` (no gather, no gravity of its own),
and on it runs `cloak`, attaches the light to its own camera and starts the hum, and at the end undoes them.

## Who owns what

| State | Owner | How it crosses |
|---|---|---|
| the fridge's knocks | the host | EventCue 133 and 134 as each starts |
| the bomb box's blast | the host | a client's open asked; the host's explosion and destroy |
| the Ariral's sounds | the host | `SoundAt`, the follower's ubergraph a host-world caller |
| a hit door's leaves | the host | `DoorDentState` after each `addDamage` |
| the float's show | the host | EventCue 135 and 136 at its `cloak` |

## Wire messages

| Kind | Direction | Carries |
|---|---|---|
| `EventCue` 133 | the host to all | the fridge's big knock starts |
| `EventCue` 134 | the host to all | the fridge's quiet knock, its volume and pitch |
| `PropActRequest` (convert) | a client to the host | the bomb box's open |
| `SoundAt` | the host to all | an Ariral's sound, by its index in the sound table |
| `DoorDentState` (344) | the host to all | a hit door's two leaves, by the door lane's key |
| `EventCue` 135, 136 | the host to all | the float's show begins at the trigger; it ends |

## Late join

The big knock happens once a session and is not replayed to a joiner; its figure behind the door is the host's to have
shown. A bomb box a client opened is gone from the host's world, which a joiner loads. A joiner gets every door the
host sent a dent for, as it stands then. A joiner while the float's show is on gets its begin.

## Measured

`[V]` (`coop/dev/entity_probe`, three peers, build 363): `fridge` mode -- the host's `startKnocking` reached slot 1
within the same second and ran there; the host's quiet knock (volume 1.00, pitch 1.00) played on slot 1. `bombbox`
mode -- slot 1's open was granted and converted on the host; the host's explosion was captured, applied on slot 1, and
the box was gone on both.

`[V]` (`coop/dev/week3_drill`, arm `lantern`, three peers, build 365): the notepad's Ariral punched two doors; the
door's metal impact, its laugh and its swing played on the host and on both clients at the host's places, and each
door's leaves were set on both clients to the host's. Before it, a client heard none of them and its doors stood shut.
`[V]` (`coop/dev/event_probe`, `agrav`, three peers, build 365): the host sent 39 lifted bodies where it had sent 2 or
3 from `buf`; slot 1 showed the float's light and hum at its begin, slot 2 joining mid-float got the begin, and both
ended the show at the host's end.

## Known limits

A client's fridge knocks start one network trip after the host's: the host looks at its fridge every frame and sends a
knock the frame it starts. The fridge's quiet knock plays its sound alone on a client; its pushes come with the props.

## Code map

| Concept | Files |
|---|---|
| the fridge's knocks | `coop/world/fridge_knock_sync` |
| the bomb box | `coop/props/prop_act_request`, `coop/props/prop_act_host` |
| the Ariral's sounds | `coop/world/sound_relay`, `coop/world/sound_catalog` |
| a hit door's leaves | `coop/interactables/door_dent_sync` |
| the float's show | `coop/world/agrav_sync` |
| the drills | `coop/dev/entity_probe` (`fridge`, `bombbox`, `sndpath`), `coop/dev/week3_drill` (`lantern`) |
