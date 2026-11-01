// ue_wrap/actors/ambient_creature.cpp -- see ue_wrap/actors/ambient_creature.h.

#include "ue_wrap/actors/ambient_creature.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/component_calls.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/sdk_profile.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"

#include <cstdint>
#include <cwchar>

namespace ue_wrap::ambient_creature {
namespace {

namespace R = ue_wrap::reflection;
namespace OI = ue_wrap::object_index;
namespace P = ue_wrap::profile;

void Collect(const wchar_t* className, std::vector<void*>& out) {
    void* cls = OI::ClassByName(className);
    if (!cls) return;
    OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
        if (R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__")) static_cast<std::vector<void*>*>(ctx)->push_back(obj);
    }, &out);
}

void* ComponentNamed(void* actor, const wchar_t* field) {
    const int32_t off = R::FindPropertyOffset(R::ClassOf(actor), field);
    if (off < 0) return nullptr;
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(actor) + off);
}

void SetVisible(void* comp, bool visible) {
    void* fn = comp ? R::FindDispatchFunctionCached(R::ClassOf(comp), P::name::SetVisibilityFn) : nullptr;
    if (!fn) return;
    ParamFrame f(fn);
    if (!f.valid()) return;
    f.Set<bool>(L"bNewVisibility", visible);
    f.Set<bool>(L"bPropagateToChildren", false);
    Call(comp, f);
}

// K2_ClearTimer, K2_PauseTimer or K2_UnPauseTimer on KismetSystemLibrary's default object, the name an FString whose count
// and capacity include the terminator.
void TimerCall(const wchar_t* verb, void* object, const wchar_t* function) {
    void* lib = R::FindClassDefaultObject(L"KismetSystemLibrary");
    void* cls = lib ? R::FindClass(L"KismetSystemLibrary") : nullptr;
    void* fn = cls ? R::FindFunction(cls, verb) : nullptr;
    if (!fn) return;
    ParamFrame f(fn);
    if (!f.valid()) return;
    f.Set<void*>(L"Object", object);
    const int32_t num = static_cast<int32_t>(std::wcslen(function)) + 1;
    struct { const wchar_t* data; int32_t n; int32_t m; } fs{function, num, num};
    if (f.SetRaw(L"FunctionName", &fs, sizeof(fs))) Call(lib, f);
}

void* WalkerAnim(void* walker) { return ComponentNamed(walker, L"anim"); }

}  // namespace

void ListSpawners(std::vector<void*>& out) {
    Collect(L"ticker_deerSpawner_C", out);
    Collect(L"ticker_treeSpawner_C", out);
    Collect(L"ticker_hexahiveSpawner_C", out);
    Collect(L"ticker_susHoleSpawner_C", out);
    Collect(L"ticker_mannequinSpawner_C", out);
}

void ListDeer(std::vector<void*>& out) { Collect(P::name::NpcClass_Deer, out); }

void ListTrees(std::vector<void*>& out) { Collect(P::name::NpcClass_WalkingTree, out); }

bool ReadTree(void* tree, float& scale, bool& landed) {
    void* mesh = ComponentNamed(tree, L"StaticMesh");
    if (!mesh) return false;
    void* scaleFn = R::FindDispatchFunctionCached(R::ClassOf(mesh), L"K2_GetComponentScale");
    void* visibleFn = R::FindDispatchFunctionCached(R::ClassOf(mesh), L"IsVisible");
    if (!scaleFn || !visibleFn) return false;
    ParamFrame s(scaleFn);
    ParamFrame v(visibleFn);
    if (!s.valid() || !v.valid() || !Call(mesh, s) || !Call(mesh, v)) return false;
    scale = s.Get<FVector>(L"ReturnValue").X;
    landed = v.Get<bool>(L"ReturnValue");
    return true;
}

void WriteTree(void* tree, float scale, bool landed) {
    void* mesh = ComponentNamed(tree, L"StaticMesh");
    if (!mesh) return;
    if (void* fn = R::FindDispatchFunctionCached(R::ClassOf(mesh), L"SetWorldScale3D")) {
        ParamFrame f(fn);
        if (f.valid()) {
            f.Set<FVector>(L"NewScale", {scale, scale, scale});
            Call(mesh, f);
        }
    }
    SetVisible(mesh, landed);
    SetVisible(ComponentNamed(tree, L"rend"), false);
}

bool Destroy(void* creature) {
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(creature), L"K2_DestroyActor");
    return fn && component_calls::CallParamless(creature, fn);
}

bool Dissolve(void* deer) {
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(deer), L"disappear");
    return fn && component_calls::CallParamless(deer, fn);
}

void ListWalkers(std::vector<void*>& out) { Collect(L"prop_wMannequin_C", out); }

void ParkWalker(void* walker) {
    engine::SetActorTickEnabled(walker, false);
    for (const wchar_t* timer : {L"timer_damage", L"timer_jump", L"timer_pickupWeapon", L"timer_saw", L"timer_unhook"})
        TimerCall(L"K2_ClearTimer", walker, timer);
}

void UnparkWalker(void* walker) { engine::SetActorTickEnabled(walker, true); }

bool ReadWalkerStanding(void* walker, bool& standing) {
    void* anim = WalkerAnim(walker);
    int32_t off = -1;
    uint8_t mask = 0;
    if (!anim || !R::FindBoolProperty(R::ClassOf(anim), L"stand", off, mask)) return false;
    standing = (static_cast<uint8_t*>(anim)[off] & mask) != 0;
    return true;
}

void WriteWalkerStanding(void* walker, bool standing) {
    void* anim = WalkerAnim(walker);
    int32_t off = -1;
    uint8_t mask = 0;
    if (!anim || !R::FindBoolProperty(R::ClassOf(anim), L"stand", off, mask)) return;
    uint8_t& b = static_cast<uint8_t*>(anim)[off];
    b = standing ? (b | mask) : (b & ~mask);
}

void ListHoles(std::vector<void*>& out) { Collect(L"susDirtHole_C", out); }

void ListBranches(std::vector<void*>& out) { Collect(L"beehiveBranch_C", out); }

void* SpawnFixture(bool branch, const FVector& at, const FRotator& rot) {
    const wchar_t* name = branch ? L"beehiveBranch_C" : L"susDirtHole_C";
    void* cls = OI::ClassByName(name);
    if (!cls) cls = R::FindClass(name);
    void* a = cls ? engine::SpawnActor(cls, at) : nullptr;
    if (a) engine::SetActorRotation(a, rot);
    return a;
}

void PauseBeehiveTimers(bool paused) {
    std::vector<void*> tickers;
    Collect(L"ticker_beehiveSpawner_C", tickers);
    for (void* t : tickers) TimerCall(paused ? L"K2_PauseTimer" : L"K2_UnPauseTimer", t, L"Spawn");
}

bool BeehiveTimerPaused() {
    std::vector<void*> tickers;
    Collect(L"ticker_beehiveSpawner_C", tickers);
    void* lib = R::FindClassDefaultObject(L"KismetSystemLibrary");
    void* cls = lib ? R::FindClass(L"KismetSystemLibrary") : nullptr;
    void* fn = cls ? R::FindFunction(cls, L"K2_IsTimerPaused") : nullptr;
    if (!fn || tickers.empty()) return false;
    ParamFrame f(fn);
    if (!f.valid()) return false;
    f.Set<void*>(L"Object", tickers[0]);
    const wchar_t* name = L"Spawn";
    struct { const wchar_t* data; int32_t n; int32_t m; } fs{name, 6, 6};
    if (!f.SetRaw(L"FunctionName", &fs, sizeof(fs)) || !Call(lib, f)) return false;
    return f.Get<bool>(L"ReturnValue");
}

}  // namespace ue_wrap::ambient_creature
