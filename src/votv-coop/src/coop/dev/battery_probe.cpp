// coop/dev/battery_probe.cpp -- [dev] the battery charger, rechargeable batteries and the car battery charger on the rig
// (VOTVCOOP_BATTERY_PROBE=1 on every peer). Times run from the probe's arm; the host ends with `[BATT] DONE`.
//   60 s  the host spawns a battery charger, plugs it in (its cordPlugged), and a rechargeable battery at 30, and
//         drains the first loose ATV battery to 20 on its own copy.
//   70 s  slot 1 puts a rechargeable battery at 10 from its hand into slot 0 (the charger's putIn, as the press does).
//   75 s  the host puts one at 40 into slot 1.
//  110 s  slot 1 takes slot 0 out (putIn on a full slot: a battery into its inventory); 115 s the host takes slot 1 out.
//  130 s  slot 1 clips the car battery charger to the first loose ATV battery, red to red and blue to blue, as its used
//         event leaves them, and plugs the charger in if it reads unplugged.
// Every peer reports every 10 s: each battery charger (its four slots, plugged, powered), the rechargeable batteries in
// the world, the car battery charger (plugged, clips), each loose ATV battery's charge and the ATVs' batteries.

#include "coop/dev/battery_probe.h"

#include "coop/config/config.h"
#include "coop/dev/drill_hand.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace coop::dev::battery_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace CR = ue_wrap::class_resolve;
using ue_wrap::FVector;

bool Enabled() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_BATTERY_PROBE") == "1";
    return on;
}

uint64_t Now() { return ::GetTickCount64(); }

void* Load(const wchar_t* cls) { return CR::ResolveClass(cls, CR::AnyFamily(), CR::Load::Immediate).cls; }

void* Spawn(const wchar_t* cls, const FVector& at) {
    void* const c = Load(cls);
    void* const a = c ? E::BeginDeferredSpawn(c, at, {}) : nullptr;
    return a && E::FinishDeferredSpawn(a, at, {}) ? a : nullptr;
}

FVector Ahead(void* me, float cm, float side = 0.f) {
    FVector at{};
    E::TryGetActorLocation(me, at);
    const FVector f = E::GetActorForwardVector(me);
    return {at.X + f.X * cm - f.Y * side, at.Y + f.Y * cm + f.X * side, at.Z};
}

std::vector<void*> AllOf(const wchar_t* cls) {
    std::vector<void*> out;
    if (void* const c = ue_wrap::object_index::ClassByName(cls))
        ue_wrap::object_index::ForEachInstance(c, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

unsigned Eid(void* a) { return static_cast<unsigned>(coop::element::Registry::Get().EidForActor(a)); }

template <class T>
T* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? reinterpret_cast<T*>(static_cast<uint8_t*>(obj) + off) : nullptr;
}

float GetF(void* obj, const wchar_t* name, float dflt = -999.f) {
    const float* p = Field<float>(obj, name);
    return p ? *p : dflt;
}

int GetBool(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(obj)[off] & mask) ? 1 : 0;
}

bool SetBool(void* obj, const wchar_t* name, bool v) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return false;
    uint8_t* b = static_cast<uint8_t*>(obj) + off;
    *b = v ? static_cast<uint8_t>(*b | mask) : static_cast<uint8_t>(*b & ~mask);
    return true;
}

bool CallWith(void* obj, const wchar_t* fn, const wchar_t* param, void* value) {
    void* const f = obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), fn) : nullptr;
    ue_wrap::ParamFrame frame(f);
    if (!f || !frame.valid()) return false;
    if (param && !frame.Set<void*>(param, value)) return false;
    return ue_wrap::Call(obj, frame);
}

// The charger's own plug event, with no cord: it sets powered, runs upd and turns its tick on.
bool PlugIn(void* charger) {
    void* const f = R::FindDispatchFunctionCached(R::ClassOf(charger), L"cordPlugged");
    ue_wrap::ParamFrame frame(f);
    return f && frame.valid() && ue_wrap::Call(charger, frame);
}

std::wstring RowOf(const wchar_t* cls) {
    Load(cls);
    void* const cdo = R::FindClassDefaultObject(cls);
    return cdo ? R::ToString(ue_wrap::prop::GetPropName(cdo)) : std::wstring();
}

void* HandActor(void* me) {
    E::MainPlayerGrabState gs{};
    return me && E::ReadMainPlayerGrabState(me, gs) ? gs.holdingActor : nullptr;
}

// A rechargeable battery into this player's hand at `energy`, then the charger's putIn into `slot`, as the press runs it.
void PutFromHand(void* me, void* charger, int slot, float energy, const char* who) {
    const std::wstring row = RowOf(L"prop_batts_acc_C");
    const bool taken = !row.empty() && coop::dev::drill_hand::TakeIntoHandNow(row.c_str(), L"prop_batts_acc_C");
    void* const held = HandActor(me);
    float* const e = Field<float>(held, L"energy");
    if (e) *e = energy;
    if (int32_t* const look = Field<int32_t>(charger, L"lookingAt")) *look = slot;
    const bool ran = CallWith(charger, L"putIn", L"inputPin", me);
    UE_LOGI("[BATT] %s: a rechargeable battery at %.0f into slot %d (hand %d, held %p, energy written %d, putIn %d)", who,
            energy, slot, taken ? 1 : 0, held, e ? 1 : 0, ran ? 1 : 0);
}

void TakeOut(void* me, void* charger, int slot, const char* who) {
    const wchar_t* const names[] = {L"b1", L"b2", L"b3", L"b4"};
    const float before = GetF(charger, names[slot]);
    if (int32_t* const look = Field<int32_t>(charger, L"lookingAt")) *look = slot;
    const bool ran = CallWith(charger, L"putIn", L"inputPin", me);
    UE_LOGI("[BATT] %s: slot %d (at %.1f) taken out into the inventory (putIn %d)", who, slot, before, ran ? 1 : 0);
}

void* g_charger = nullptr;

void Clip(void* me) {
    const std::vector<void*> chargers = AllOf(L"prop_carBatteryCharger_C");
    const std::vector<void*> bats = AllOf(L"prop_atvcarbattery_C");
    void* const ch = chargers.empty() ? nullptr : chargers[0];
    void* const bat = bats.empty() ? nullptr : bats[0];
    if (!ch || !bat) {
        UE_LOGW("[BATT] client: no car battery charger (%zu) or loose ATV battery (%zu) to clip", chargers.size(),
                bats.size());
        return;
    }
    // As the used event leaves a clip on a pin: the battery named on its side, the pin's flag on the battery.
    if (void** const r = Field<void*>(ch, L"carBatteryRed")) *r = bat;
    if (void** const b = Field<void*>(ch, L"carBatteryBlue")) *b = bat;
    SetBool(ch, L"pluggedRedToRed", true);
    SetBool(ch, L"pluggedBlueToRed", false);
    SetBool(bat, L"plugged_red", true);
    SetBool(bat, L"plugged_blue", true);
    const int plugged = GetBool(ch, L"plugged");
    bool plugRan = false;
    if (plugged == 0) plugRan = PlugIn(ch);
    UE_LOGI("[BATT] client: the car battery charger clipped to battery eid=%u at %.2f (charger plugged %d, plugged now %d)",
            Eid(bat), GetF(bat, L"battery"), plugged, plugRan ? 1 : 0);
}

void Report(const char* role) {
    for (void* ch : AllOf(L"prop_batteryCharger_C")) {
        const int32_t* const plugged = Field<int32_t>(ch, L"plugged");
        UE_LOGI("[BATT] %s charger eid=%u slots %.1f %.1f %.1f %.1f plugged %d powered %d", role, Eid(ch), GetF(ch, L"b1"),
                GetF(ch, L"b2"), GetF(ch, L"b3"), GetF(ch, L"b4"), plugged ? *plugged : -1, GetBool(ch, L"powered"));
    }
    std::string acc;
    for (void* b : AllOf(L"prop_batts_acc_C")) {
        char s[48];
        std::snprintf(s, sizeof(s), " %u:%.1f", Eid(b), GetF(b, L"energy"));
        acc += s;
    }
    UE_LOGI("[BATT] %s rechargeables in the world:%s", role, acc.empty() ? " none" : acc.c_str());
    for (void* ch : AllOf(L"prop_carBatteryCharger_C")) {
        void** const r = Field<void*>(ch, L"carBatteryRed");
        void** const b = Field<void*>(ch, L"carBatteryBlue");
        UE_LOGI("[BATT] %s car charger eid=%u plugged %d red->%u blue->%u redToRed %d blueToRed %d done %d", role, Eid(ch),
                GetBool(ch, L"plugged"), r && *r ? Eid(*r) : 0u, b && *b ? Eid(*b) : 0u, GetBool(ch, L"pluggedRedToRed"),
                GetBool(ch, L"pluggedBlueToRed"), GetBool(ch, L"doneCharging"));
    }
    std::string cars;
    for (void* b : AllOf(L"prop_atvcarbattery_C")) {
        char s[48];
        std::snprintf(s, sizeof(s), " %u:%.2f", Eid(b), GetF(b, L"battery"));
        cars += s;
    }
    std::string atvs;
    for (void* a : AllOf(L"ATV_C")) {
        char s[48];
        std::snprintf(s, sizeof(s), " %.2f", GetF(a, L"battery"));
        atvs += s;
    }
    UE_LOGI("[BATT] %s loose ATV batteries:%s; ATVs' batteries:%s", role, cars.empty() ? " none" : cars.c_str(),
            atvs.empty() ? " none" : atvs.c_str());
}

uint64_t g_armedAt = 0, g_reportAt = 0;
int g_step = 0;
bool g_done = false;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!Enabled() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const unsigned slot = host ? 0u : static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId());
    if (!g_armedAt) {
        g_armedAt = Now();
        UE_LOGI("[BATT] probe armed role=%s slot=%u", host ? "host" : "client", slot);
    }
    const uint64_t t = Now() - g_armedAt;
    void* const me = coop::players::Registry::Get().Local();
    if (me && host) {
        if (g_step == 0 && t >= 60000) {
            g_step = 1;
            g_charger = Spawn(L"prop_batteryCharger_C", Ahead(me, 150.f));
            const bool plugged = g_charger && PlugIn(g_charger);
            void* const loose = Spawn(L"prop_batts_acc_C", Ahead(me, 120.f, 80.f));
            if (float* const e = Field<float>(loose, L"energy")) *e = 30.f;
            UE_LOGI("[BATT] host: a battery charger %p (plugged in %d) and a rechargeable battery at 30 %p", g_charger,
                    plugged ? 1 : 0, loose);
            // The first loose ATV battery drained to 20 on the host, so the car charger has a charge to raise.
            const std::vector<void*> cars = AllOf(L"prop_atvcarbattery_C");
            float* const charge = cars.empty() ? nullptr : Field<float>(cars[0], L"battery");
            if (charge) *charge = 20.f;
            UE_LOGI("[BATT] host: loose ATV battery eid=%u drained to 20 here (%d)", cars.empty() ? 0u : Eid(cars[0]),
                    charge ? 1 : 0);
        }
        if (g_step == 1 && t >= 75000) {
            g_step = 2;
            PutFromHand(me, g_charger, 1, 40.f, "host");
        }
        if (g_step == 2 && t >= 115000) {
            g_step = 3;
            TakeOut(me, g_charger, 1, "host");
        }
    } else if (me && slot == 1) {
        void* const ch = [] {
            const std::vector<void*> all = AllOf(L"prop_batteryCharger_C");
            return all.empty() ? nullptr : all[0];
        }();
        if (g_step == 0 && t >= 70000) {
            g_step = 1;
            if (ch) PutFromHand(me, ch, 0, 10.f, "client");
            else UE_LOGW("[BATT] client: no battery charger here at 70 s");
        }
        if (g_step == 1 && t >= 110000) {
            g_step = 2;
            if (ch) TakeOut(me, ch, 0, "client");
        }
        if (g_step == 2 && t >= 130000) {
            g_step = 3;
            Clip(me);
        }
    }
    if (host && !g_done && t >= 280000) {
        g_done = true;
        UE_LOGI("[BATT] DONE");
    }
    if (t < 55000 || t > 290000 || Now() < g_reportAt) return;
    g_reportAt = Now() + 10000;
    char role[16];
    std::snprintf(role, sizeof(role), "%s%u", host ? "host" : "slot", slot);
    Report(role);
}

}  // namespace coop::dev::battery_probe
