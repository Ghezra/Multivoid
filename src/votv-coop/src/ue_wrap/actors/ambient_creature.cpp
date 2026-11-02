// ue_wrap/actors/ambient_creature.cpp -- see ue_wrap/actors/ambient_creature.h.

#include "ue_wrap/actors/ambient_creature.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/component_calls.h"
#include "ue_wrap/core/kismet_timer.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/sdk_profile.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <cstdint>

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

// A bool field's place in a class, looked up when the class changes. An instance's class pointer names that instance's class
// whatever else has lived at the address, so a match cannot read another class's layout.
struct BoolField {
    const wchar_t* name;
    void* cls = nullptr;
    int32_t off = -1;
    uint8_t mask = 0;
    uint8_t* At(void* obj) {
        void* c = R::ClassOf(obj);
        if (c != cls) {
            cls = c;
            if (!R::FindBoolProperty(c, name, off, mask)) off = -1;
        }
        return off < 0 ? nullptr : static_cast<uint8_t*>(obj) + off;
    }
};
BoolField g_saw{L"saw"};
BoolField g_looking{L"looking"};
BoolField g_stand{L"stand"};

void* WalkerAnim(void* walker) {
    static void* s_cls = nullptr;
    static int32_t s_off = -1;
    void* c = R::ClassOf(walker);
    if (c != s_cls) {
        s_cls = c;
        s_off = R::FindPropertyOffset(c, L"anim");
    }
    return s_off < 0 ? nullptr : *reinterpret_cast<void**>(static_cast<uint8_t*>(walker) + s_off);
}

// The native functions and library objects the view test calls: they last the process, and two of the lookups walk the object
// table, while the test runs twenty times a second. A miss retries at most once a second.
struct ViewCalls {
    void* sysCdo = nullptr;
    void* libCdo = nullptr;
    void* bounds = nullptr;
    void* frustum = nullptr;
    std::vector<R::ParamInfo> frustumParams;
    uint64_t nextTryMs = 0;
    bool Ready() {
        if (bounds && frustum && frustumParams.size() >= 4) return true;
        const uint64_t now = ::GetTickCount64();
        if (now < nextTryMs) return false;
        nextTryMs = now + 1000;
        if (!sysCdo) sysCdo = R::FindClassDefaultObject(L"KismetSystemLibrary");
        if (!libCdo) libCdo = R::FindClassDefaultObject(L"bpCodeLib");
        void* sysCls = sysCdo ? R::FindClass(L"KismetSystemLibrary") : nullptr;
        void* libCls = libCdo ? R::FindClass(L"bpCodeLib") : nullptr;
        if (!bounds && sysCls) bounds = R::FindFunction(sysCls, L"GetComponentBounds");
        if (!frustum && libCls) frustum = R::FindFunction(libCls, L"isBoxInFrustum");
        if (frustum && frustumParams.empty()) frustumParams = R::FunctionParams(frustum);
        return bounds && frustum && frustumParams.size() >= 4;
    }
};
ViewCalls g_view;

}  // namespace

void ListSpawners(std::vector<void*>& out) {
    Collect(L"ticker_deerSpawner_C", out);
    Collect(L"ticker_treeSpawner_C", out);
    Collect(L"ticker_hexahiveSpawner_C", out);
    Collect(L"ticker_susHoleSpawner_C", out);
    Collect(L"ticker_mannequinSpawner_C", out);
    Collect(L"ticker_bushSpawning_C", out);
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
        kismet_timer::Clear(walker, timer);
}

void UnparkWalker(void* walker) { engine::SetActorTickEnabled(walker, true); }

bool ReadWalkerStanding(void* walker, bool& standing) {
    void* anim = WalkerAnim(walker);
    const uint8_t* b = anim ? g_stand.At(anim) : nullptr;
    if (!b) return false;
    standing = (*b & g_stand.mask) != 0;
    return true;
}

void WriteWalkerStanding(void* walker, bool standing) {
    void* anim = WalkerAnim(walker);
    uint8_t* b = anim ? g_stand.At(anim) : nullptr;
    if (b) *b = standing ? (*b | g_stand.mask) : (*b & static_cast<uint8_t>(~g_stand.mask));
}

bool WalkerInView(void* walker, void* playerController) {
    void* mesh = ComponentNamed(walker, L"skeletalMesh");
    if (!mesh || !playerController) return false;
    void* renderedFn = R::FindDispatchFunctionCached(R::ClassOf(mesh), L"WasRecentlyRendered");
    if (!renderedFn || !g_view.Ready()) return false;
    ParamFrame bounds(g_view.bounds);
    if (!bounds.valid()) return false;
    bounds.Set<void*>(L"Component", mesh);
    if (!Call(g_view.sysCdo, bounds)) return false;
    const FVector origin = bounds.Get<FVector>(L"Origin");
    const FVector extent = bounds.Get<FVector>(L"BoxExtent");
    // The library's parameter names are the game's own: by their order, controller, origin, extent, return.
    const std::vector<R::ParamInfo>& names = g_view.frustumParams;
    ParamFrame frustum(g_view.frustum);
    if (!frustum.valid()) return false;
    frustum.Set<void*>(names[0].name.c_str(), playerController);
    frustum.Set<FVector>(names[1].name.c_str(), origin);
    frustum.Set<FVector>(names[2].name.c_str(), extent);
    if (!Call(g_view.libCdo, frustum) || !frustum.Get<bool>(names[3].name.c_str())) return false;
    ParamFrame rendered(renderedFn);
    if (!rendered.valid()) return false;
    rendered.Set<float>(L"Tolerance", 3.f);
    return Call(mesh, rendered) && rendered.Get<bool>(L"ReturnValue");
}

void HoldWalkerSeen(void* walker) {
    if (uint8_t* b = g_saw.At(walker)) *b |= g_saw.mask;
}

void ReleaseWalkerSeen(void* walker) {
    if (uint8_t* b = g_saw.At(walker)) *b &= static_cast<uint8_t>(~g_saw.mask);
}

bool ReadWalkerLooking(void* walker) {
    const uint8_t* b = g_looking.At(walker);
    return b && (*b & g_looking.mask) != 0;
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

bool PauseBeehiveTimers(bool paused) {
    std::vector<void*> tickers;
    Collect(L"ticker_beehiveSpawner_C", tickers);
    bool settled = !tickers.empty();
    for (void* t : tickers) {
        if (paused) kismet_timer::Pause(t, L"Spawn");
        else kismet_timer::UnPause(t, L"Spawn");
        settled = settled && kismet_timer::IsPaused(t, L"Spawn") == paused;
    }
    return settled;
}

bool BeehiveTimerPaused() {
    std::vector<void*> tickers;
    Collect(L"ticker_beehiveSpawner_C", tickers);
    return !tickers.empty() && kismet_timer::IsPaused(tickers[0], L"Spawn");
}

}  // namespace ue_wrap::ambient_creature
