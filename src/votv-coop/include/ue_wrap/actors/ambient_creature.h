// ue_wrap/actors/ambient_creature.h -- the deer and the walking tree: their spawners, a tree's size and landing, and a
// deer's dissolve. Each class is read by reflection on the game thread.

#pragma once

#include <vector>

namespace ue_wrap::ambient_creature {

// The live spawners of the two classes, the deer's and the tree's.
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

}  // namespace ue_wrap::ambient_creature
