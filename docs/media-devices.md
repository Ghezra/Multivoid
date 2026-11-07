# Media devices

## Purpose

The placed radios and TVs, and the ATV's radio: who owns what each plays and how a peer's pick reaches the others.
The other keyed devices around the base are in [devices.md](devices.md).

## How it works

A placed radio or TV (`coop/world/media_device_sync`) plays through each peer's own media player and picks its own next track, so the
host's state stands: a client's copy is moved to its track, time and play state, and a client with the radio's interface
open sends what its player changed. A track crosses as a hash of its catalog link and opens only from the peer's own
catalog `[V]` (`harness/autotest/autotest_radio_state.cpp`: a pick, stop, play and mode from a client, a pick from the
host and a rejoin, with silent test tracks, the rule written on and the pick by the radio's own function). A TV also
carries its channel, volume, brightness and squeeze; its openLink needs its power as well as the rule, so a track waits
for its cord `[V]` (`harness/autotest/autotest_tv_state.cpp`: the same from each peer with a channel and the picture, with
a silent audio-only `.mp4`, powered by a write, so no picture was decoded). The ATV's radio is one of them, a child actor
of the ATV named by the ATV's key (`atv:<key>`) `[V]` (`harness/autotest/autotest_atv_host.cpp`: a pick from each peer
through the radio's own interface, read off its media player on both; on the code before it, each pick stayed on its
own peer).

## Wire messages

| Message | Direction | Carries |
|---|---|---|
| `MediaDeviceState` | the host to all; a client to the host | a device's track (a hash of its catalog link), time, play state and mode, and a TV's channel and picture; a client's change through its interface |

## Late join

Every device again at the joiner's ready edge.

## Code map

| What | Where |
|---|---|
| the lane | `coop/world/media_device_sync` |
| the device's own parts | `ue_wrap/world/media_device` |
