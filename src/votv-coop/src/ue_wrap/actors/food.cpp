// ue_wrap/actors/food.cpp -- see ue_wrap/actors/food.h.

#include "ue_wrap/actors/food.h"

#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/component_calls.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"

#include <cstring>
#include <unordered_map>

namespace ue_wrap::food {
namespace {

namespace R = reflection;

constexpr float kKelvinToCelsius = 273.f;  // the tick's own constant, not 273.15
constexpr float kSteamAboveCelsius = 50.f;
constexpr float kFrostOnMesh = 0.5f;
constexpr int32_t kFrostDataIndex = 1;

std::unordered_map<void*, bool> g_isFoodClass;
// A field's offset, read once per declaring class. An InstanceOffset keeps one class and resolves again on every change
// of class, and a poll over mixed foods changes class on nearly every read.
class FamilyField {
public:
    FamilyField(const wchar_t* family, const wchar_t* name) : family_(family), name_(name) {}
    int32_t Of(void* actor) {
        void* base = nullptr;
        for (void* c = R::ClassOf(actor); c && !base; c = R::SuperStructOf(c))
            if (R::NameEquals(R::NameOf(c), family_)) base = c;
        if (!base) return -1;
        if (base != base_) {
            base_ = base;
            off_ = R::FindPropertyOffset(base, name_);
        }
        return off_;
    }

private:
    const wchar_t* family_;
    const wchar_t* name_;
    void* base_ = nullptr;
    int32_t off_ = -1;
};

FamilyField g_temperature{L"prop_food_C", L"temperature"};
FamilyField g_ripeness{L"prop_food_C", L"ripeness"};
FamilyField g_uses{L"prop_food_C", L"uses"};
FamilyField g_celsius{L"prop_food_C", L"celsius"};
FamilyField g_steam{L"prop_food_C", L"eff_steam"};
FamilyField g_cook{L"prop_cookingFood_C", L"cookProgress"};
FamilyField g_impact{L"prop_C", L"physicsImpact"};

void* ReadObjectField(FamilyField& field, void* actor) {
    const int32_t off = field.Of(actor);
    if (off < 0) return nullptr;
    void* value = nullptr;
    std::memcpy(&value, static_cast<const uint8_t*>(actor) + off, sizeof(value));
    return value && R::IsLive(value) ? value : nullptr;
}

bool ComponentActive(void* component) {
    void* const fn = R::FindDispatchFunctionCached(R::ClassOf(component), L"IsActive");
    if (!fn) return false;
    ParamFrame f(fn);
    return f.valid() && Call(component, f) && f.Get<bool>(L"ReturnValue");
}

bool FrozenField(void* actor, uint8_t*& byte, uint8_t& mask) {
    int32_t off = -1;
    if (!R::FindBoolProperty(R::ClassOf(actor), L"tempLessThan0", off, mask) || off < 0 || !mask) return false;
    byte = static_cast<uint8_t*>(actor) + off;
    return true;
}

void PaintFrozen(void* actor, bool frozen) {
    if (void* mesh = prop::GetStaticMesh(actor)) {
        if (void* fn = R::FindDispatchFunctionCached(R::ClassOf(mesh), L"SetCustomPrimitiveDataFloat")) {
            ParamFrame f(fn);
            if (f.valid() && f.Set<int32_t>(L"DataIndex", kFrostDataIndex) &&
                f.Set<float>(L"Value", frozen ? kFrostOnMesh : 0.f))
                Call(mesh, f);
        }
    }
    if (void* impact = ReadObjectField(g_impact, actor)) {
        static R::InstanceOffset sOverride{L"physmat_override"};
        const int32_t off = sOverride.Of(impact);
        if (off >= 0) {
            void* material = frozen ? R::FindObject(L"concrete", L"PhysicalMaterial") : nullptr;
            std::memcpy(static_cast<uint8_t*>(impact) + off, &material, sizeof(material));
        }
        component_calls::CallParamlessNamed(impact, L"makePhysSound");
    }
    if (void* fn = R::FindDispatchFunctionCached(R::ClassOf(actor), L"freezed")) {
        ParamFrame f(fn);
        if (f.valid() && f.Set<bool>(L"frozen", frozen)) Call(actor, f);
    }
}

}  // namespace

bool IsFoodClass(void* cls) {
    if (!cls) return false;
    auto it = g_isFoodClass.find(cls);
    if (it != g_isFoodClass.end()) return it->second;
    bool food = false;
    for (void* c = cls; c && !food; c = R::SuperStructOf(c)) food = R::NameEquals(R::NameOf(c), L"prop_food_C");
    g_isFoodClass.emplace(cls, food);
    return food;
}

bool IsFood(void* actor) { return actor && R::IsLive(actor) && IsFoodClass(R::ClassOf(actor)); }

bool Read(void* actor, Condition& out) {
    if (!IsFood(actor)) return false;
    const int32_t temperature = g_temperature.Of(actor), ripeness = g_ripeness.Of(actor), uses = g_uses.Of(actor);
    if (temperature < 0 || ripeness < 0 || uses < 0) return false;
    const auto* base = static_cast<const uint8_t*>(actor);
    std::memcpy(&out.temperature, base + temperature, sizeof(float));
    std::memcpy(&out.ripeness, base + ripeness, sizeof(float));
    std::memcpy(&out.uses, base + uses, sizeof(int32_t));
    const int32_t cook = g_cook.Of(actor);
    out.cookProgress = -1.f;
    if (cook >= 0) std::memcpy(&out.cookProgress, base + cook, sizeof(float));
    return true;
}

bool Write(void* actor, const Condition& in) {
    if (!IsFood(actor)) return false;
    const int32_t temperature = g_temperature.Of(actor), ripeness = g_ripeness.Of(actor), uses = g_uses.Of(actor),
                  celsius = g_celsius.Of(actor);
    if (temperature < 0 || ripeness < 0 || uses < 0 || celsius < 0) return false;
    auto* base = static_cast<uint8_t*>(actor);
    const float c = in.temperature - kKelvinToCelsius;
    std::memcpy(base + temperature, &in.temperature, sizeof(float));
    std::memcpy(base + ripeness, &in.ripeness, sizeof(float));
    std::memcpy(base + uses, &in.uses, sizeof(int32_t));
    std::memcpy(base + celsius, &c, sizeof(float));
    const int32_t cook = g_cook.Of(actor);
    if (cook >= 0 && in.cookProgress >= 0.f) std::memcpy(base + cook, &in.cookProgress, sizeof(float));
    return true;
}

bool PaintLooks(void* actor) {
    Condition now;
    if (!Read(actor, now)) return false;
    const float celsius = now.temperature - kKelvinToCelsius;
    if (void* steam = ReadObjectField(g_steam, actor)) {
        const bool want = celsius > kSteamAboveCelsius;
        if (ComponentActive(steam) != want) {
            if (void* fn = R::FindDispatchFunctionCached(R::ClassOf(steam), L"SetActive")) {
                ParamFrame f(fn);
                if (f.valid() && f.Set<bool>(L"bNewActive", want) && f.Set<bool>(L"bReset", true)) Call(steam, f);
            }
        }
    }
    uint8_t* byte = nullptr;
    uint8_t mask = 0;
    if (!FrozenField(actor, byte, mask)) return true;
    const bool frozen = celsius <= 0.f;
    if (((*byte & mask) != 0) != frozen) {
        *byte = frozen ? (*byte | mask) : (*byte & ~mask);
        PaintFrozen(actor, frozen);
    }
    return true;
}

bool ReadLooks(void* actor, bool& steam, bool& frozen) {
    if (!IsFood(actor)) return false;
    void* const component = ReadObjectField(g_steam, actor);
    uint8_t* byte = nullptr;
    uint8_t mask = 0;
    if (!component || !FrozenField(actor, byte, mask)) return false;
    steam = ComponentActive(component);
    frozen = (*byte & mask) != 0;
    return true;
}

}  // namespace ue_wrap::food
