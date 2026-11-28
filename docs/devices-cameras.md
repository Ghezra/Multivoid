# Security cameras: who watches, where they point

Which player watches a security camera, where it points, its name, and the TV that shows it. A camera's two saved bits, `active` and `isLight`, are on
[devices.md](devices.md).

## What the game does

A security camera (`prop_camera_bad_C`, and its `prop_camera_C`, `_good_C`, `_ultra_C`, `_s_C` and `_cursed_C`
children; a Kerfur Omega carries a `prop_camera_good_C` as a child actor) shows its LED by `upd`: red while not
`active`, blue while `active` and not `spec`, green while both. The laptop's `setCam` clears `spec` on the camera it
held and sets it on the one picked, on that machine alone.

Its aim is three targets, `rot_x`, `rot_y` and `zoom`. The laptop's controls write them through `setAngle` (all three;
only `zoom` while `ignoreMove`) and `addAngle` (added to each), and both run `move`, which turns the camera's `head`
and both lenses' field of view toward the targets a step a frame, clamps the targets to the camera's ranges and plays
its motor sounds while turning. The save keeps `rot_x` and `rot_y`.

A camera's name (`cameraName`) is `CAM_<n>` while it has none, numbered by its place in that machine's own list of
cameras, so the same camera is numbered apart on each peer. The laptop's camera slot (`uicomp_camSlot_C`) writes it when
its name box is committed. A TV (`prop_tv2_C` and its children) shows a camera from its interface's search box:
`bindCamera(name)` finds the camera of that name in the gamemode's `allSecurityCameras` and points the camera's
captures at the TV, and `setAsSecurity` turns the TV to it. The TV saves neither.

## How it works

The watch light (`coop/interactables/camera_watch_sync`, Ghezra/Multivoid issue 57): another player's watching never
showed, and leaving the cameras panel, or picking a drone or a Kerfur, left the last camera green. Each peer looks four
times a second at what its own player watches: a laptop open on its cameras panel with a camera picked, or nothing. A
client sends a change to the host. The host keeps each player's camera, its own too; a camera is watched while any
player watches it, and the host writes `spec` and runs `upd` on its own copy and sends each change to every client.
After a client's own `setCam`, the host's bits are written back over it.

A Kerfur's camera is a child actor with no key of its own, so its key is its Omega's element id with `#cam`
(`interactable_sync::CameraKeyOf`), the same on every peer.

The aim (`coop/interactables/camera_aim_sync`, issue 60): each machine turned its own copy only. A watch after each
`setAngle` and `addAngle` marks the camera. A client's own turn runs at once, and the camera's three targets go to the
host at most ten times a second (`CameraAimAsk`). The host writes them on its camera and runs `move`; after its own
turn or a client's it sends the targets to every other player, at most ten times a second per camera
(`CameraAimState`), and a client writes them and runs `move`, so the head turns there as it turned on the turner's.

The names and the TVs (`coop/interactables/camera_screen_sync`, issue 54): a renamed camera kept its old name on the
others, a TV showed its camera on its own machine alone, and a client that typed the host's name found no camera, its
own copy being named apart. A watch after the camera slot's commit sends a client's rename to the host
(`CameraNameAsk`); the host writes it and sends it to the others (`CameraNameState`). A watch after a TV's
`setAsSecurity` sends the camera the TV now shows, or none, by the camera's key and the TV's element id
(`TvCameraAsk`); the host binds its own TV and sends it to the others (`TvCameraState`). Each peer binds a TV by its own
copy of the camera's name.

## Who owns what

| State | Owner | How it crosses |
|---|---|---|
| a camera's watched light | the host | each player's watched camera reported; the union sent |
| a camera's aim | the host | a client's turn asked with its targets; the targets sent to the others |
| a camera's name | the host | a client's rename asked; the name sent to the others, every name to a joiner |
| the camera a TV shows | the host | a client's pick asked; the TV's camera sent to the others |

## Wire messages

| Kind | Direction | Carries |
|---|---|---|
| `CameraWatchAsk` (342) | a client to the host | the camera its player watches, or none |
| `CameraWatchState` (343) | the host to all | a camera watched or no longer watched |
| `CameraAimAsk` (345) | a client to the host | a camera it turned and its `rot_x`, `rot_y` and `zoom` |
| `CameraAimState` (346) | the host to all but the turner, or a joiner | a camera's targets |
| `CameraNameAsk` (347) | a client to the host | a camera it renamed and its name |
| `CameraNameState` (348) | the host to all but the renamer, or a joiner | a camera's name |
| `TvCameraAsk` (349) | a client to the host | a TV and the camera it shows, or none |
| `TvCameraState` (350) | the host to all but the setter, or a joiner | a TV and the camera it shows, or none |

## Late join

A joiner gets every watched camera, and every camera turned this session as it points; a camera's saved aim comes with
the save. It also gets every camera's name, so its own numbering gives way to the host's, and every TV set this
session. A camera it cannot name yet (a Kerfur's, before its Omega's element id) takes its aim and name once it can.

## Measured

`[V]` (`coop/dev/entity_probe`, three peers, build 364): `camwatch` mode -- slot 1 opened its laptop on the cameras
panel and picked the Kerfur's camera: green on all three; another panel turned it off, the cameras panel again green,
closing the laptop off; a joiner got the light.

`[V]` (build 365): `camaim` mode -- slot 1's `setAngle` and ten `addAngle(2, 0, 0)` on the Kerfur's camera (which has
`ignoreMove`, so only the zoom of a `setAngle` takes) reached the host (rot_x 20, zoom 60), the host's later `setAngle`
reached slot 1 (zoom 80), and slot 2, joining later, took both once its Kerfur camera could be named. `camscreen` mode
-- the host's and the clients' four cameras were numbered apart (the Kerfur's `CAM_0002` on the host, `CAM_0003` on
slot 1) and both clients took the host's names at join; slot 1 bound a TV the host spawned to the camera by its own
name, and the host's TV showed it; the host's turn of the TV back reached slot 1 and slot 2. A rename through a camera
slot's own name box is `[?]`: the drill's world held no camera slot.

## Known limits

A player at the laptop when a host's turn arrives sees its own controls' values replaced. Two players turning one camera
at once each see the last turn the host took.

## Code map

| Concept | Files |
|---|---|
| the watch light | `coop/interactables/camera_watch_sync`, `ue_wrap/devices/security_camera` |
| the aim | `coop/interactables/camera_aim_sync` |
| the names and the TVs | `coop/interactables/camera_screen_sync` |
| the camera keys | `coop/interactables/interactable_sync` |
| the drills | `coop/dev/entity_probe` (`camwatch`, `camaim`, `camscreen`) |
