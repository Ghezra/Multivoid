# More events

The basement fridge's knocks and the Arirals' bomb box. The other story events, the weather and the sounds played at a
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

## How it works

The fridge (`coop/world/fridge_knock_sync`): each player's fridge rolled its own dice against its own player, so a
client never heard the host's knocks nor felt the shake (Ghezra/Multivoid issue 61). A client's two timers are now
refused, and the host looks at its fridge ten times a second: `knocked` newly set is the big knock, sent as EventCue
133; `knockamount` spent to 0 is the quiet knock, sent as EventCue 134 with its sound's volume and pitch. A client runs
its own `startKnocking` for the big knock (the knocks, the shake on its own player, the figure) and plays its quiet
knock sound at the host's volume and pitch. The props the quiet knock pushes are the host's, on their own lane.

The bomb box (`coop/props/prop_act_request`, `prop_act_host`): a client's blast was its own world's, which the
explosion lane removes there, and no request reached the host, so it never exploded (issue 64). A client's action 4
on the box is now asked of the host as a conversion is: the host runs the box's own `actionOptionIndex`, its explosion
reaches every peer through the explosion lane, and its destroy removes the box everywhere.

## Who owns what

| State | Owner | How it crosses |
|---|---|---|
| the fridge's knocks | the host | EventCue 133 and 134 as each starts |
| the bomb box's blast | the host | a client's open asked; the host's explosion and destroy |

## Wire messages

| Kind | Direction | Carries |
|---|---|---|
| `EventCue` 133 | the host to all | the fridge's big knock starts |
| `EventCue` 134 | the host to all | the fridge's quiet knock, its volume and pitch |
| `PropActRequest` (convert) | a client to the host | the bomb box's open |

## Late join

The big knock happens once a session and is not replayed to a joiner; its figure behind the door is the host's to have
shown. A bomb box a client opened is gone from the host's world, which a joiner loads.

## Measured

`[V]` (`coop/dev/entity_probe`, three peers, build 363): `fridge` mode -- the host's `startKnocking` reached slot 1
within the same second and ran there; the host's quiet knock (volume 1.00, pitch 1.00) played on slot 1. `bombbox`
mode -- slot 1's open was granted and converted on the host; the host's explosion was captured, applied on slot 1, and
the box was gone on both.

## Known limits

A client's own fridge figure and knocks run on its own clock, a few hundred milliseconds off the host's. The fridge's
quiet knock plays its sound alone on a client; its pushes come with the props.

## Code map

| Concept | Files |
|---|---|
| the fridge's knocks | `coop/world/fridge_knock_sync` |
| the bomb box | `coop/props/prop_act_request`, `coop/props/prop_act_host` |
| the drill | `coop/dev/entity_probe` (`fridge`, `bombbox`) |
