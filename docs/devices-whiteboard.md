# Whiteboards

## Purpose

A whiteboard's drawing is the world's, on every peer: a stroke drawn on one peer shows on the others, and a peer joining
later sees the picture as it stands.

## How it works

A placed whiteboard is `prop_whiteboard_C`, whose `whiteboardobj` field is its drawing surface, the native `whiteboard_C`.
The surface owns the render target the player paints into and the game's own serialization of the finished picture:
`TextureToString` renders the target as base64 text, `stringToTexture` puts one back. The prop's `getData` banks that text
in the save slot, so the text is the picture's identity on both ends.

The lane reads each board's picture once a second, and four times a second while a stroke runs. Two encodes of the same
untouched board are not byte-identical, so the comparison is the picture's **length**: a stroke, an eraser pass or a clear
moves it, and a still board does not. A picture whose length moved is sent to the host as `BoardImage`; the host applies it
and forwards the same bytes to every other peer, which apply them the same way. A picture that came in over the wire is
never sent back, so two peers cannot echo one board at each other.

A board's picture is one blob of `BlobChunkPayload` parts; the lane reassembles them itself, by part index, so a repeated
or reordered part cannot upset the count.

## Who owns what

The host is the relay's hub. Any peer may draw, and its picture reaches the others through the host. Two peers drawing on
one board both land: the picture is the whole board, so the later one already holds the earlier one's strokes.

The host's board is the relay's hub, not the only writer: any peer may draw, and its picture reaches the others through
the host. Two peers drawing at once both land, last picture wins, which is what drawing on one board is.

## Wire messages

| Kind | Direction | Payload | Meaning |
|---|---|---|---|
| `BoardImage` (351) | client → host, host → all | `BlobChunkPayload` | a board's picture: `[u64 board key][u32 length][base64 text]` |

The key is FNV-1a 64 over the board's object path, the same on every peer. A part is exact length; the lane reassembles,
checks the header against the blob's size, and drops a picture naming a board the world does not hold. Never relayed past
the host, never sent before the world is up.

## Late join

At its world-ready a joiner is sent every board's picture, one blob each, before the props snapshot drains. A peer joining
mid-stroke sees the board as it stood at the last finished stroke.

## Known limits

- One picture per change, not a live line: the surface draws inside its own Blueprint tick (`lineTrace`, `transformPoint`,
  `makeTris`, `K2_DrawMaterialTriangle`), so replaying a stroke would re-implement that math. A stroke shows on the other
  peers within about a second `[V]`, and the whole picture is re-sent each time.
- A picture past the chunked lane's ceiling (255 parts, deflated when it fits) is refused once and named in the log.
- A board keyed by its prop's sync key: a board destroyed and placed again is a new board to this lane.

## Code map

| Where | What |
|---|---|
| `src/ue_wrap/world/whiteboard.cpp` | the props and surfaces, the picture's read and write |
| `src/coop/world/whiteboard_sync.cpp` | the stroke trigger, the blob send, the host's relay, the join replay, the clear safety net |