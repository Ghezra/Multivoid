# Fuel machines

## Purpose

The machines a gas can fuels and a button starts: the woodchipper and the portable generator. Each keeps its fuel and its
on/off in its own record and burns fuel in its own tick, so each ran on every copy for itself; the host's copy is now the
machine, and a client's pour and press are asked of it. The base's own generator (`generator_C`, no fuel) is the power
grid's, in [devices.md](devices.md).

## The woodchipper

A woodchipper keeps its fuel (0 to 100) and whether it is on. A gas can fills it through its `gascanFuel`, its button
flips it on or off, its own tick burns fuel while it runs and switches it off at 0, and a prop that enters its intake
box is destroyed and chipped into a garbage clump. Each of those ran only on the copy it happened on, so a client's pour
or press never reached the host, whose copy the save keeps, and a host's never reached a client `[V]`. The host's copy is
now the machine (`coop/interactables/woodchipper_sync`, `coop/interactables/woodchipper_gate`):

- A client's copy is refused its tick and its intake at the script gate, as the foods' and the plants' ticks are; the
  host's copy eats, destroys the prop through the prop destroy path and drops the clump `[?]`. A client's held or thrown
  prop is a host copy its drive keeps unsimulated, which `canChip` refuses; at the overlap's resume the host sets it
  simulating while the chipper is on, idle and fuelled, and the chipper takes it `[V]`; a refused copy is set back `[?]`.
- Every change of the host's chipper is announced by the game's own `checkActive`, and by the button's body for a press
  at fuel 0, so a watch on each sends the chipper's fuel and on/off at once, one message per changed chipper per tick. The
  burn is the one thing nothing announces: while a chipper's fuel moves it is read and sent once a second. A client
  writes both onto its copy and, when the on/off changed, runs the game's own `checkActive` on it, so the copy's motor and
  its sounds start as in single player `[?]`; a state for a chipper it has not resolved yet is held, the newest per key,
  and the held keys are retried when the prop index moves.
- A client's pour runs on its copy as the game wrote it (its can drains and sounds), and the fuel the copy gained is
  sent to the host; its press of the button is refused on the copy and sent as the state it wanted. The host judges each
  (the sender's world, the chipper by key and class, a living sender, its reach, a bounded queue and rate, the gas can
  in the sender's hand and the room in the tank for a pour, the state wanted for a press), adds the fuel it has room for
  and runs `checkActive`, or runs the game's own press with the sender's puppet as the player, inside the requester
  feedback scope, and answers a refused intent with the state, to the sender alone, whenever it can name the chipper (a
  key that names none has no state to send). A client can refill a chipper to full on demand, at the intent budget of
  four a second; it cannot overfill it, and no gas can drains but the one in the client's own hand `[?]`.
- At a joiner's ready edge the host sends which chippers are on; their fuel comes with the prop records, and the prop a
  chipper took or left is the ordinary prop state, no flip pending. A mid-chip chipper shows no chip animation or sound `[?]`.

`[?]` until the drills run (`autotest_woodchipper.cpp`, the `woodchipper_drill` row; `autotest_chipper_feed.cpp`, the flip).

## The portable generator

`prop_generatorP_C` keeps its fuel and `isActive` in its record; a can fills it (`gascanFuel`, or one lying in its fuel box),
action 8 runs `tryActivate` and `activateOutlets` powers its two sockets, and its tick burns fuel. Each copy ran its own, so a
client's pour or start never reached the host (Trello #50) `[V]`. Now (`coop/interactables/generator_sync`) a client's copy is
refused its tick and the fuel box; its pour and press run on its copy and go to the host (`GeneratorIntent`), which checks the
key and class, a living sender in reach and, for a pour, the can in its hand, then adds the fuel or runs `tryActivate`. The
host reads its generators twice a second and sends one that changed, a running one once a second, a refused intent's state
to its sender and every one to a joiner (`GeneratorState`); a client writes it, with `activateOutlets` on an on/off change.
`[V]` `coop/dev/genp_probe` (`VOTVCOOP_GENP_PROBE=client`): the client's pour of 50 and its start ran on the host, whose burn the
client's copy then followed; before it the host's copy stayed empty and off.
