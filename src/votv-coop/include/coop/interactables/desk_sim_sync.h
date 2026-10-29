// coop/interactables/desk_sim_sync.h -- the signal-desk download sim as a host-authored stream.
//
// THE ROOT: the download-rate formula (AanalogDScreenTest) rolls TWO UNSEEDED RNG terms per tick,
// the detector needle DL_resDetecPercent and a transient noise, and integrates the filter offsets
// from per-peer frame dt, so the OUTPUTS diverge across peers even from identical knob inputs.
// Streaming them on the occupant-authored, claim-gated DeskState cannot fix that: unclaimed means
// no stream and self-divergence, and a client occupant would author shared-world RNG. Seeding is
// impossible against an unseeded roll plus transient noise, so the host owns the simulation and
// streams the output vector (DeskSimPose, about 10 Hz, newest-wins, interpolated like the cursor);
// the client overwrites its own local sim, whose garbage the overwrite hides.
//
// The knob INTENTS (speeds, active, dir) stay occupant-authored on DeskState and the host's own
// blueprint integrates the offset, so this vector is host-down only: one author. frData and poData
// ride it rather than converging natively.

#pragma once

#include <cstdint>

// The needle's crossing is the host's too. A client's own step would cross first and run the whole crossing block,
// the looker_behind autoSave included, so its step is parked (its multiplier held at 0) and its autoSave refused. The
// host counts its crossings and carries the count, its canDL and the download's identity in the same snapshot as
// the needle, which orders each edge with the state; the client runs its desk's own path after the step on a moved
// count, and canSaveSignal on a changed canDL. MTA orders a server's edges with its sync the same way, by a counter
// the sync carries (reference/mtasa-blue/Server/mods/deathmatch/logic/CElement.cpp:1281-1305).

namespace coop::net { class Session; }

namespace coop::desk_sim_sync {

void Install(coop::net::Session* session);

// Game thread, per pump tick. HOST: read the live sim outputs, its crossing count and canDL, and publish them
// through Session::SetHostDeskSim, whose net thread fans out DeskSimPose. CLIENT: park the desk's step, drain the
// host's vector, interpolate PER CHANNEL -- each channel keeps its own deadline, and an unchanged target that
// ARRIVES snaps cur to target exactly, where a window shared across channels and reopened by every packet kept the
// detector's bitwise 1.0 from ever landing -- then WriteSimOutputs, a raw write every tick. A snapshot that carries
// an edge snaps the needle and decoded instead of easing them, and its painters run after the write: the first
// snapshot's lasting ones (canSaveSignal, setFullyProcessedSignalObject), a moved count's path after the step, a
// changed canDL's canSaveSignal, and the lasting ones again for a download this client forms for the signal the host
// last crossed on (a joiner's). The vector is 7 channels; coord_cooldown belongs to desk_input_sync.
void Tick();

void OnDisconnect();

// For the drill: the host's crossings this session and the client's runs of the path after the step. Game thread.
uint32_t HostCrossings();
uint32_t ClientPostSteps();

}  // namespace coop::desk_sim_sync
