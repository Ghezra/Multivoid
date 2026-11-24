# The printer camera, the tape and custom pictures

A printed photo shows the host's picture on every peer and a client's print is filed in the host's save; a tape's repair
of a prop is the host's; a picture frame's or poster's custom picture is the host's choice, by its file name. The identity, birth and drop of the props themselves are on [props.md](props.md).

## What the game does

A camera's click (`prop_digcam_C`'s left click, or its action 4, through its own arrow) calls the gamemode's `photo`,
which captures the view and, two frames on, appends the picture (a JPEG, 100 to 112 KB on the rig) to the save object's
`photos`, the laptop's gallery. The printer camera (`prop_digcam_print_C`, which inherits it) has `canPrint` set: its
`makePhoto`, 0.2 s on, takes a paper (`prop_notebook_paper_C`) from the pocket through `checkPaper`, copies the
gallery's last picture into `photos_phys` at the same index, and pockets a `prop_photo_C` whose `index` names it; the
photo's `loadImage` shows `photos_phys[index]`, and its record saves only the index. Without a paper it says so. The
index is a save's own: a photo that crossed showed the receiver's picture of that index, or none, and a client's print,
born on its machine alone, carried its own gallery's index into the world when dropped.

The office tape (`prop_officetape_C`), held, keeps the prop its holder looks at in `prop`; its left click sets that
prop's `health` (on its `comp_physicsImpact_C`) to its full (`damageData.health`) and takes one of its `uses`, going at
none, or says the prop is fine when it is at full. A prop's health is the host's (`coop/props/prop_damage_gate`), so a
client's repair mended its own copy and the host's prop broke as before.

A picture frame (`prop_picture_h_C`, and its `_s`, `_t` and `_v` sizes, which inherit it), a held poster
(`prop_poster_C`), a poster on a wall (`poster_C`), a held custom rug (`prop_rug_c_C`) and a laid one (`rug_c_C`),
pressed with the world's custom content on, open the gamemode's
picker over the player's own custom pictures (the files in `%LOCALAPPDATA%\VotV\Assets\paintings\<size>` and
`posters` and `rugs`). The pick (`intComs_texturePickerApply`) sets the piece's `filename` and paints it: a frame through
`upd`, a held poster through `setTex`, a wall poster through `setPoster`, a rug through `setSkin`, each loading that
file. A sticker sheet's pick only names the next sticker it presses, whose record carries the name. Each saves `filename` in its
record. A pick set it on the picker's machine alone.

## How it works

The camera (`coop/props/photo_sync`): the host's save owns every picture index. Every picture the host's photo copies
load while a client is connected, its own prints first, is sent to every client with its index (`PhotoImage` op 2), in
parts of 50 KB on chunked blobs. A client's `makePhoto` is refused and run by the lane 0.25 s on: its own `checkPaper`,
then its capture sent to the host (op 1). The host judges it (the camera within 6 m of the sender's body, one print a
second), appends it to its own gallery, so its later prints never take that index, writes it to `photos_phys` there
(`ue_wrap/actors/photo_store`) and sends it to every client naming the printer. A client writes a picture to its own
`photos_phys` at the host's index and has every photo copy of that index load it again; the printer spawns its photo at
that index and pockets it, as the game's print does. With no paper the game's own `makePhoto` runs, for its hint. The
host's own capture appends to its gallery at once and its `makePhoto` prints the gallery's last entry 0.2 s later, so a
client's print that arrives in between waits 0.6 s; filed there, it would be printed as the host's.

The tape (`coop/props/tape_sync`): a client's click on a prop the host names is refused and asked of the host
(`TapeAsk`). The host judges it (the prop within 6 m of the sender's body, four asks a second), mends its own prop or
finds it at full, takes the use from its copy of the tape and answers the sender (`TapeState`). The answer runs the
tape's own click on the client's copy, the prop's copy set to what the host found, so the hint, the sound, the uses and,
at the last use, the tape's own going (as a held cement bag's last unit goes) are the game's. A click with no prop in
view runs as the game's, and the host's own repairs run as the game's.

Custom pictures (`coop/props/custom_picture_sync`): a watch after each picker's apply reads the piece's new `filename`.
A client's pick is sent to the host (`PictureAsk`), naming a frame, held poster or held rug by its element and a wall
poster or laid rug by its save key; the picture itself never crosses. The host judges it (the piece within 8 m of the sender's body, four asks
a second), writes the name on its own piece, paints it with the piece's own function and sends the name to every client
(`PictureState`); its own picks are sent the same way. A client writes the name on its copy and paints it, which loads
that file from its own folder, so every player is taken to have the same custom picture files. A refused pick is
answered with the host's name, which takes it back. A poster put on a wall, a laid rug and a sticker carry their
picture's name in their build record ([deployables.md](deployables.md)).

## Who owns what

| State | Owner | How it crosses |
|---|---|---|
| a printed photo's picture | the host's save | each picture sent once to every client, and every one to a joiner |
| a client's print | the host | its capture asked; the host files it and the printer pockets the photo |
| the gallery a capture appends to | each machine | a client's print is also appended to the host's |
| a prop's health under the tape | the host | a client's click asked; the answer runs the click on its copy |
| the tape's uses | the host | in each answer |
| a frame's, poster's or rug's custom picture | the host | a client's pick asked by its file name; the name sent to all |
| the custom picture files | each player's own folder | never; every player is taken to have the same files |

## Wire messages

| Kind | Direction | Carries |
|---|---|---|
| `PhotoImage` | a client to the host (op 1); the host to all, or to a joiner (op 2) | a client's capture with its camera; a picture with its index and its printer |
| `TapeAsk` | a client to the host | the tape and the prop it names |
| `TapeState` | the host to the asker | mended, at full or refused, and the tape's uses |
| `PictureAsk` | a client to the host | a frame, poster or rug and the picked file name |
| `PictureState` | the host to all, a joiner or a refused asker | a piece and its file name |

## Late join

A joiner's save carries no pictures, so the host's connect replay sends it every printed picture in its save. It also
sends every frame, held poster and held rug with a custom picture by its name; wall posters and laid rugs come with
their build records.

## Measured

`[V]` (`coop/dev/entity_probe`, `VOTVCOOP_ENTITY_PROBE=photo`, three peers on build 356): slot 1's print with a printer
camera was filed in the host's save as photo 0 (102 KB) and pocketed on slot 1; the host's own print became photo 1; both
pictures hashed the same on the host, slot 1 and a slot that joined after both (from the connect replay), and a photo the
host laid in the world showed photo 1's picture on all three; slot 1's click with no paper printed nothing. Slot 1's tape
on a bottle at half its health mended the host's bottle to full and took one use of the host's tape; its second click
found it at full. Screenshots of slot 1's photo laid in the world show the same picture on the host, on slot 1 and on
slot 2. A run where the host's own print and slot 1's crossed filed
slot 1's picture as the host's: the hold above came from it; its rerun did not cross again. Before it, no peer's picture reached another, and a joiner's copy of a printed photo was blank.

`[V]` (`picture` mode, three peers on build 360, two test pictures in each custom folder): slot 1's picks on a frame, a
held poster and a wall poster showed the same file names on the host; the host's later pick on the frame reached slot 1;
slot 2, joining after the first two picks, got all three names and then the host's pick. Before it, a pick reached no
other peer. In `rugs` mode, slot 1's picks on a held rug and a laid rug reached the host, the host's later pick on the
laid rug reached slot 1, and a slot joining later got both names.

## Known limits

The flash and the shutter sound are the taker's alone. A client's gallery keeps only its own captures, and the host's
laptop shows a client's print only after its next load. The digital camera (`prop_digcam_C`) prints nothing, so its
captures stay with the taker's laptop. Every picture of the host's save goes to each joiner, about 100 KB each. A tape's uses on a peer that has not
used it are its own until its first use, so its right-click count may be stale. A custom picture whose file another
player lacks shows that player's blank or default picture; a file name over 94 characters stays on its picker's
machine.

## Code map

| Concept | Files |
|---|---|
| the camera and the photo | `coop/props/photo_sync`, `ue_wrap/actors/photo_store` |
| the tape | `coop/props/tape_sync` |
| custom pictures | `coop/props/custom_picture_sync` |
| the drill | `coop/dev/entity_probe` (`photo`, `picture`) |
