// ue_wrap/actors/ambient_creature.h -- the deer and the walking tree: their spawners, a tree's size and landing, and a
// deer's dissolve. Each class is read by reflection on the game thread.

#pragma once

#include "ue_wrap/core/types.h"

#include <vector>

namespace ue_wrap::ambient_creature {

// The live spawners a client's tick poll parks: the deer's, the tree's, the hexahive's, the sus hole's, the mannequin's and the bush's.
void ListSpawners(std::vector<void*>& out);

// Their live creatures.
void ListDeer(std::vector<void*>& out);
void ListTrees(std::vector<void*>& out);

// A tree's mesh scale and whether it has landed: its mesh shows from the landing on.
bool ReadTree(void* tree, float& scale, bool& landed);

// A copy never lands by itself, as its movement does not run: this writes what the landing and the begin play did.
void WriteTree(void* tree, float scale, bool landed);

bool Destroy(void* creature);

// The deer's own dissolve; it ends in the deer's destroy.
bool Dissolve(void* deer);

// The walking mannequins.
void ListWalkers(std::vector<void*>& out);

// A copy's brain off: its tick, which walks it and reads this peer's view, and the timers its begin play set, which jump, hit
// and take up weapons. Its AI pawn's move loop keeps asking for paths, and nothing reads them.
void ParkWalker(void* walker);
void UnparkWalker(void* walker);

// Its anim's stand: the pose it holds when watched.
bool ReadWalkerStanding(void* walker, bool& standing);
void WriteWalkerStanding(void* walker, bool standing);

// The walker's own view test, as its tick runs it: inside this controller's frustum and rendered of late.
bool WalkerInView(void* walker, void* playerController);

// Its saw flag holds it frozen, as its tick reads it, until its own timer clears the flag. An angry walker does not freeze.
void HoldWalkerSeen(void* walker);
void ReleaseWalkerSeen(void* walker);
bool ReadWalkerLooking(void* walker);

// Is this actor a walker?
bool IsWalker(void* actor);

// Its angry flag: an angry walker hits a player it touches, and does not freeze.
bool ReadWalkerAngry(void* walker);
void WriteWalkerAngry(void* walker);

// The points its tick walks, and the index of the next. The tick walks them only while its AI pawn's controller has a path of
// its own, so WalkerEnsureMove asks for one toward `dest` when that controller has none.
bool WriteWalkerPath(void* walker, const std::vector<FVector>& points, bool restart);
void WalkerEnsureMove(void* walker, const FVector& dest);

// A hexahive's and a sus hole's dirt holes, and the beehive spawner's branches.
void ListHoles(std::vector<void*>& out);
void ListBranches(std::vector<void*>& out);

// A plain copy of either: a branch spawned this way makes no nest of its own.
void* SpawnFixture(bool branch, const FVector& at, const FRotator& rot);

// The sus hole spawner's own gate: it rolls only while the game instance's mode is 0 or 1. Game thread.
bool HolesRoll();

// The sus hole spawner's own test of a spot: a trace 10 m above `point` to 10 m below it against static geometry, kept when it
// hits grass. `at` is the hit and `rot` its normal as a rotator, as the spawner places the hole. Game thread.
bool HoleSpot(const FVector& point, FVector& at, FRotator& rot);

// The beehive spawner's repeating timer, which has no tick to take off. True once a spawner stands and every one reads as asked:
// the timer arms a moment into the world, so a pause before that takes nothing.
bool PauseBeehiveTimers(bool paused);

// Whether a spawner's timer stands paused; false with no spawner.
bool BeehiveTimerPaused();

}  // namespace ue_wrap::ambient_creature
