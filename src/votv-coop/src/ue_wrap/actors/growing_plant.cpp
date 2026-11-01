// ue_wrap/actors/growing_plant.cpp -- see ue_wrap/actors/growing_plant.h.

#include "ue_wrap/actors/growing_plant.h"

#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/component_calls.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"

namespace ue_wrap::growing_plant {
namespace {

namespace R = ue_wrap::reflection;
namespace OI = ue_wrap::object_index;

// ForEachInstance walks one exact class.
constexpr const wchar_t* kFamily[] = {
    L"growingPlant_C",          L"growingPlant_shroom_7_C",  L"growingPlant_pine_C",     L"growingPlant_orangetree_C",
    L"growingPlant_lemontree_C", L"growingPlant_grassPatch_C", L"growingPlant_deadtree_C", L"growingPlant_stolasHerb_C",
};

template <class T>
T* Field(void* plant, const wchar_t* name) {
    const int32_t off = R::FindPropertyOffset(R::ClassOf(plant), name);
    return off < 0 ? nullptr : reinterpret_cast<T*>(static_cast<uint8_t*>(plant) + off);
}

bool ReadBool(void* plant, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    return R::FindBoolProperty(R::ClassOf(plant), name, off, mask) && (static_cast<uint8_t*>(plant)[off] & mask) != 0;
}

void WriteBool(void* plant, const wchar_t* name, bool on) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!R::FindBoolProperty(R::ClassOf(plant), name, off, mask)) return;
    uint8_t& b = static_cast<uint8_t*>(plant)[off];
    b = on ? (b | mask) : (b & ~mask);
}

std::wstring ReadName(void* plant, const wchar_t* name) {
    const R::FName* f = Field<R::FName>(plant, name);
    return f ? R::ToString(*f) : std::wstring();
}

void WriteName(void* plant, const wchar_t* name, const std::wstring& value) {
    if (R::FName* f = Field<R::FName>(plant, name)) *f = fname_utils::StringToFName(value);
}

void CallWithBool(void* plant, const wchar_t* fnName, const wchar_t* param, bool value) {
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(plant), fnName);
    if (!fn) return;
    ParamFrame f(fn);
    if (!f.valid()) return;
    f.Set<bool>(param, value);
    Call(plant, f);
}

void CallPlain(void* plant, const wchar_t* fnName) {
    if (void* fn = R::FindDispatchFunctionCached(R::ClassOf(plant), fnName)) component_calls::CallParamless(plant, fn);
}

}  // namespace

void List(std::vector<void*>& out) {
    for (const wchar_t* name : kFamily) {
        void* cls = OI::ClassByName(name);
        if (!cls) continue;
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__")) static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    }
}

std::wstring Key(void* plant) {
    std::wstring key = prop::GetActorSaveKeyString(plant);
    if (!key.empty() && key != L"None") return key;
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(plant), L"getKey");
    if (!fn) return L"";
    ParamFrame f(fn);
    if (!f.valid() || !Call(plant, f)) return L"";
    key = prop::GetActorSaveKeyString(plant);
    return key == L"None" ? L"" : key;
}

std::wstring Row(void* plant) { return ReadName(plant, L"plant"); }

std::wstring AttachParent(void* plant) {
    std::wstring key = ReadName(plant, L"attachParent");
    return key == L"None" ? L"" : key;
}

bool HideDirt(void* plant) { return ReadBool(plant, L"hideDirt"); }

bool Read(void* plant, Values& out) {
    const float* grow = Field<float>(plant, L"grow");
    const float* fertilizer = Field<float>(plant, L"fertilizer");
    const float* water = Field<float>(plant, L"water");
    const float* fruitGrow = Field<float>(plant, L"fruitGrow");
    const float* dryness = Field<float>(plant, L"dryness");
    const int32_t* amount = Field<int32_t>(plant, L"amount");
    if (!grow || !fertilizer || !water || !fruitGrow || !dryness || !amount) return false;
    out = Values{*grow, *fertilizer, *water, *fruitGrow, *dryness, *amount};
    return true;
}

void Apply(void* plant, const std::wstring& row, const Values& v) {
    Values old;
    if (!Read(plant, old)) return;
    const bool rowChanged = Row(plant) != row;
    *Field<float>(plant, L"grow") = v.grow;
    *Field<float>(plant, L"fertilizer") = v.fertilizer;
    *Field<float>(plant, L"water") = v.water;
    *Field<float>(plant, L"fruitGrow") = v.fruitGrow;
    *Field<float>(plant, L"dryness") = v.dryness;
    *Field<int32_t>(plant, L"amount") = v.amount;
    if (rowChanged) WriteName(plant, L"plant", row);

    const bool nowGrown = v.grow >= 1.f;
    const bool grewEdge = nowGrown && !ReadBool(plant, L"grew");
    const bool fruitEdge = (old.fruitGrow >= 1.f) != (v.fruitGrow >= 1.f) || old.amount != v.amount;
    if (rowChanged || fruitEdge) CallWithBool(plant, L"upd", L"skipDataTable", !rowChanged);
    // upd's own read of a ripe bush sets amount; the sender's count stands.
    *Field<int32_t>(plant, L"amount") = v.amount;
    if (grewEdge || (rowChanged && nowGrown)) {
        WriteBool(plant, L"grew", true);
        CallWithBool(plant, L"processGrown", L"skipGrowCheck", true);
    } else if (!nowGrown && ReadBool(plant, L"grew")) {
        WriteBool(plant, L"grew", false);
    }
    CallPlain(plant, L"updLoc");
}

void* Spawn(const std::wstring& className, const std::wstring& key, const std::wstring& row, const std::wstring& attachParent,
            bool hideDirt, const FVector& at, const FRotator& rot) {
    void* cls = OI::ClassByName(className.c_str());
    if (!cls) cls = R::FindClass(className.c_str());
    if (!cls) return nullptr;
    void* plant = engine::BeginDeferredSpawn(cls, at, rot);
    if (!plant) return nullptr;
    WriteName(plant, L"key", key);
    WriteName(plant, L"plant", row);
    WriteBool(plant, L"hideDirt", hideDirt);
    if (!engine::FinishDeferredSpawn(plant, at, rot)) return nullptr;
    // Begin play clears this with no owner; processKeys finds the pot by it, attaches and joins the two.
    if (!attachParent.empty()) {
        WriteName(plant, L"attachParent", attachParent);
        if (void* fn = R::FindDispatchFunctionCached(R::ClassOf(plant), L"processKeys")) {
            ParamFrame f(fn);
            if (f.valid()) Call(plant, f);
        }
    }
    return plant;
}

bool Destroy(void* plant) {
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(plant), L"K2_DestroyActor");
    return fn && component_calls::CallParamless(plant, fn);
}

}  // namespace ue_wrap::growing_plant
