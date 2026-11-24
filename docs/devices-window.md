# The bay window

The base's bay window keeps its grime as a picture. The dabs that clean it reach every peer as they land, and the
host's picture is what every peer's window shows. The other windows' single dirt value is on [devices.md](devices.md).

## What the game does

The bay window (`d_window_C`, one in the world, with no save key) draws its grime into a 1645x512 render target. Its
begin play sets a looping 120 s timer on `dirtify`, which runs `dirty`: a splotch of `mat_cleanbrush_glassdirt` at a
random place, size and angle, refused while a stroke session is open or a signal plays. A sponge or mop that touches
the glass (`cleanPhys`, through a thrown sponge's `cleanball_C` too) or a `cleanAtPoint` draws a dab of the sponge's
brush: it stores the dab's corner in `A`, opens the window's canvas session through its `Canvas` event (`cv`; the
session closes itself 0.25 s after the last dab) and draws with `UCanvas::K2_DrawMaterial`. `getCode` encodes the
target (format 2, quality 90; about 260 KB); the gamemode saves it as the save slot's `windowShitBytes`, and its load
hands it to `setCode`. Each peer's timer drew its own random splotches, so every peer had its own grime to clean.

## How it works

The dabs (`coop/interactables/window_stroke_sync`): a stroke reaches `cleanPhys` through a script call no hook here
carries arguments for, so the dab is observed at its native draw instead: a `K2_DrawMaterial` whose calling frame is
the window with its session open, which the splotch cannot be, since it refuses to run then. The stroking peer sends
the dab's pixel, its edge and the brush's opacity and colour (`WindowStroke`); every other peer draws the same dab
through the window's own `Canvas` event with a brush material the mod owns, and the host relays a client's dab. A dab
with no sponge in its player's hand (a thrown sponge's) is not sent.

The grime (`coop/interactables/window_dirt_sync`): a client's `dirtify` is refused, so only the host's timer draws
splotches. The host sends its window's whole picture, its own `getCode`, to every client in parts (`WindowImage`) when
it changes: 0.3 s after a splotch, and once the dabs on its window, its own or replayed, have stopped for 1.5 s, at
most once a second and never the same picture twice; nothing while the window is untouched. A joiner gets it at its
connect. A client draws the picture over its whole target inside the window's own `Canvas` session, as a dab is drawn,
once its own player's dabs have stopped for 0.6 s and no session is open.

Never `setDraw`, `endDraw`, `setCode` or `dirty` from outside: the session belongs to the window, and an outside
`endDraw` (`setCode` runs one of its own) deletes the world canvas the game's own next dab draws into
(`ue_wrap/devices/window_canvas`).

## Who owns what

| State | Owner | How it crosses |
|---|---|---|
| a dab | the peer whose sponge drew it | `WindowStroke`, the host relaying a client's |
| a splotch | the host | in the host's next picture |
| the window's picture | the host | `WindowImage` after a change, and to a joiner |

## Wire messages

| Kind | Direction | Carries |
|---|---|---|
| `WindowStroke` | any peer to the host, the host to all | a dab: its corner, its edge, the brush's opacity and colour |
| `WindowImage` | the host to all, or to a joiner (chunked) | the window's whole picture, in parts of 50 KB |

## Late join

A joiner's save carries the window as the host last saved it; the host's connect replay sends the picture as it is.

## Measured

`[V]` (`coop/dev/entity_probe`, `VOTVCOOP_ENTITY_PROBE=grime`, three peers on build 361): the host's six splotches
went out as one picture (259 KB) at once; slot 1's own `dirtify` and every later timer call on both clients were
refused; slot 1 drew the host's picture within 2 s, and slot 2, joining later, drew the picture at its connect. A
client's target does not hash as the host's sent bytes (its `getCode` encodes the drawn picture again), but slot 1 and
slot 2 read the same bytes once they had drawn the same picture. Five `cleanAtPoint` dabs of a sponge no one held, which
the stroke lane cannot send, went out in the host's picture 1.5 s after the last, and slot 1 drew it a second later.
The three peers' pictures, written to files at the end, show the same grime. Each send cost the host's tick 7 to 12 ms (the
target's read back).

## Known limits

Between a dab and the host's next picture a client's window may show a thrown sponge's dab only where it landed.
A client's own dabs drawn within 0.6 s before a picture arrives are redrawn by the host's picture once the host has
them. Each picture costs every client about 260 KB.

## Code map

| Concept | Files |
|---|---|
| the dabs | `coop/interactables/window_stroke_sync` |
| the grime picture | `coop/interactables/window_dirt_sync` |
| the window's canvas, its picture | `ue_wrap/devices/window_canvas` |
| the drill | `coop/dev/entity_probe` (`grime`) |
