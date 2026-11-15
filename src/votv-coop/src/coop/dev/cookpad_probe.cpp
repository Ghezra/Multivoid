// coop/dev/cookpad_probe.cpp -- [dev] a food a client sets down on a cooking pad, on both peers (VOTVCOOP_COOKPAD_PROBE=1).
//
// The pad (prop_cookingPad_C) cooks in its own tick, while powered and active, every prop_cookingFood_C its `box`
// overlaps. HOST, 20 s after a client is connected: spawns a pad VOTVCOOP_COOKPAD_AHEAD uu (default 50) before the client's puppet at its middle's
// height, frozen there as on a counter (the client freezes its copy at 24 s), sets it powered and active (its `upd`),
// and a raw steak beside the puppet. CLIENT: at 26 s takes the steak into its hand through the player's own
// `Hold Object` (VOTVCOOP_COOKPAD_ROUTE=drop or place), or grabs it through the use key's own grab chain (grab, the
// default); from 28 s turns its view each frame onto the pad's centre, VOTVCOOP_COOKPAD_LIFT uu above it (default 0),
// so the grab sets the steak on the pad and the placing drop's trace meets it; at 34 s lets it go: dropGrabObject for
// the grab, simulateDrop for the others, with place set for place. BOTH, every half second from 20 s to 70 s: the pad's place, and each steak's place,
// its height over the pad, whether its body simulates, its cookProgress and temperature (`[COOKPAD]`).

#include "coop/dev/cookpad_probe.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/hand_item.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_drive.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/prop_flags.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace coop::dev::cookpad_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;

constexpr const wchar_t* kPadClass   = L"prop_cookingPad_C";
constexpr const wchar_t* kPadPath    = L"/Game/objects/prop_cookingPad.prop_cookingPad_C";
constexpr const wchar_t* kSteakClass = L"prop_cookingFood_rawSteak_C";
constexpr const wchar_t* kSteakPath  = L"/Game/objects/prop_cookingFood_rawSteak.prop_cookingFood_rawSteak_C";

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_COOKPAD_PROBE") == "1";
    return on;
}

// How the client lets the steak go: drop (the drop key), place (a placing drop at the aim) or grab (a physics grab
// let go over the pad).
const std::string& Route() {
    static const std::string r = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_COOKPAD_ROUTE");
        return v.empty() ? std::string("grab") : v;
    }();
    return r;
}

// How far before the client's puppet the pad stands (VOTVCOOP_COOKPAD_AHEAD, default 50 uu): the grab holds a prop
// about as far from the eye as it was when taken, so the grab route sets the pad out where that hold point falls.
float Ahead() {
    static const float a = [] {
        const std::string v = coop::config::ReadEnv("VOTVCOOP_COOKPAD_AHEAD");
        return v.empty() ? 50.f : static_cast<float>(std::atof(v.c_str()));
    }();
    return a;
}

bool Floor() {
    static const bool f = coop::config::ReadEnv("VOTVCOOP_COOKPAD_FLOOR") == "1";
    return f;
}

float Lift() {
    static const float lift = static_cast<float>(std::atof(coop::config::ReadEnv("VOTVCOOP_COOKPAD_LIFT").c_str()));
    return lift;
}

void* Class(const wchar_t* name, const wchar_t* path) {
    void* cls = OI::ClassByName(name);
    return cls ? cls : ue_wrap::asset_load::LoadObjectByPath(path);
}

void SetBool(void* obj, const wchar_t* name, bool v) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return;
    uint8_t& b = static_cast<uint8_t*>(obj)[off];
    b = v ? static_cast<uint8_t>(b | mask) : static_cast<uint8_t>(b & ~mask);
}

void* Obj(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? *reinterpret_cast<void* const*>(static_cast<const char*>(obj) + off) : nullptr;
}

float Float(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? *reinterpret_cast<const float*>(static_cast<const char*>(obj) + off) : -1.f;
}

std::vector<void*> All(const wchar_t* name) {
    std::vector<void*> out;
    void* const cls = OI::ClassByName(name);
    if (cls)
        OI::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

void* First(const wchar_t* name) {
    const std::vector<void*> all = All(name);
    return all.empty() ? nullptr : all.front();
}

// The view turned onto `at` from the player's eye, so the hand's hold point lies on that line.
void LookAt(const ue_wrap::FVector& at) {
    void* const me = coop::players::Registry::Get().Local();
    void* const pc = me ? E::GetController(me) : nullptr;
    ue_wrap::FVector eye{};
    if (!pc || !E::TryGetActorLocation(me, eye)) return;
    eye.Z += 64.f;
    const float dx = at.X - eye.X, dy = at.Y - eye.Y, dz = at.Z - eye.Z;
    const float yaw = std::atan2(dy, dx) * 57.29578f;
    const float pitch = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 57.29578f;
    E::SetControlRotation(pc, {pitch, yaw, 0.f});
}

void Report() {
    void* const pad = First(kPadClass);
    ue_wrap::FVector padAt{};
    const bool padRead = pad && E::TryGetActorLocation(pad, padAt);
    UE_LOGI("[COOKPAD] pad %hs at (%.1f, %.1f, %.1f)", pad ? "live" : "none", padAt.X, padAt.Y, padAt.Z);
    void* const me = coop::players::Registry::Get().Local();
    void* const hand = Obj(me, L"holding_actor");
    for (void* it : All(kSteakClass)) {
        ue_wrap::FVector at{};
        E::TryGetActorLocation(it, at);
        int mirrorOf = -1;
        for (uint8_t sl = 0; sl < coop::net::kMaxPeers; ++sl)
            if (coop::hand_item::MirrorActorForSlot(sl) == it) mirrorOf = sl;
        char tag[48] = "";
        if (it == hand) snprintf(tag, sizeof(tag), " (in this hand)");
        else if (mirrorOf >= 0) snprintf(tag, sizeof(tag), " (mirror of slot %d's hand)", mirrorOf);
        UE_LOGI("[COOKPAD] steak key=%ls at (%.1f, %.1f, %.1f) over the pad %.1f uu, simulating=%d cookProgress=%.3f "
                "temperature=%.1f%hs",
                ue_wrap::prop::GetInteractableKeyString(it).c_str(), at.X, at.Y, at.Z, padRead ? at.Z - padAt.Z : 0.f,
                E::IsActorRootSimulatingPhysics(it) ? 1 : 0, Float(it, L"cookProgress"), Float(it, L"temperature"), tag);
    }
}

uint64_t g_startAt = 0, g_reportAt = 0;
int g_step = 0;
coop::director::AimedGrab* g_grab = nullptr;
bool g_frozen = false;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[COOKPAD] probe armed role=%s lift=%.0f", host ? "host" : "client", Lift());
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    if (host && g_step == 0 && t >= 20000u) {
        coop::RemotePlayer& rp = coop::puppet_drive::Puppet(1);
        void* const puppet = rp.valid() ? rp.GetActor() : nullptr;
        ue_wrap::FVector at{};
        if (puppet && E::TryGetActorLocation(puppet, at)) {   // a late joiner's puppet is waited for
            g_step = 1;
            const ue_wrap::FVector f = E::GetActorForwardVector(puppet);
            void* const pad = E::SpawnActor(Class(kPadClass, kPadPath),
                                            {at.X + f.X * Ahead(), at.Y + f.Y * Ahead(), Floor() ? at.Z - 40.f : at.Z});
            // At a counter's height, held there as a counter would: frozen on this copy and, at 24 s, on the client's.
            // On the floor (VOTVCOOP_COOKPAD_FLOOR=1) it rests loose, as a pad on a counter does.
            if (!Floor()) ue_wrap::prop::CallSetPropProps(pad, false, true, false, false);
            SetBool(pad, L"powered", true);
            SetBool(pad, L"active", true);
            const bool upd = pad && R::CallFunction(pad, R::FindDispatchFunctionCached(R::ClassOf(pad), L"upd"), nullptr);
            void* const steak = E::SpawnActor(Class(kSteakClass, kSteakPath),
                                              {at.X - f.Y * 70.f, at.Y + f.X * 70.f, at.Z - 20.f});
            UE_LOGI("[COOKPAD] host: spawned the pad (ok=%d, upd=%d) and the steak (ok=%d)", pad ? 1 : 0, upd ? 1 : 0,
                    steak ? 1 : 0);
        }
    }
    if (!host && g_step == 0 && t >= 24000u && !g_frozen && !Floor()) {
        g_frozen = true;
        void* const pad = First(kPadClass);
        UE_LOGI("[COOKPAD] client: froze its copy of the pad (ok=%d)",
                pad && ue_wrap::prop::CallSetPropProps(pad, false, true, false, false) ? 1 : 0);
    }
    if (!host && g_step == 0 && t >= 26000u && Route() == "grab") {
        // The use key's own grab chain, aimed at the steak, held at the aim until the pad turn.
        void* const steak = First(kSteakClass);
        if (!g_grab && steak && me) g_grab = new coop::director::AimedGrab(me, steak);
        if (g_grab) {
            const coop::director::GrabState g = g_grab->Tick();
            if (g != coop::director::GrabState::Working) {
                UE_LOGI("[COOKPAD] client: grabbed the steak (%hs%hs)", g == coop::director::GrabState::Grabbed ? "ok" : "failed: ",
                        g == coop::director::GrabState::Grabbed ? "" : g_grab->Why());
                delete g_grab;
                g_grab = nullptr;
                g_step = 1;
            }
        }
    }
    if (!host && g_step == 0 && t >= 26000u && Route() != "grab") {
        g_step = 1;
        void* const steak = First(kSteakClass);
        ue_wrap::ParamFrame f(me ? R::FindDispatchFunctionCached(R::ClassOf(me), L"Hold Object") : nullptr);
        const bool ran = steak && f.valid() && f.Set<bool>(L"useHold", true) && f.Set<void*>(L"manual", steak) &&
                         ue_wrap::Call(me, f);
        UE_LOGI("[COOKPAD] client: took the steak into the hand through Hold Object (ran=%d, hand holds %ls)", ran ? 1 : 0,
                Obj(me, L"holding_actor") ? R::ClassNameOf(Obj(me, L"holding_actor")).c_str() : L"nothing");
    }
    if (!host && g_step == 1 && t >= 28000u && t < 34000u) {
        ue_wrap::FVector padAt{};
        if (void* const pad = First(kPadClass); pad && E::TryGetActorLocation(pad, padAt))
            LookAt({padAt.X, padAt.Y, padAt.Z + Lift()});
    }
    if (!host && g_step == 1 && t >= 34000u) {
        g_step = 2;
        ue_wrap::FVector padAt{}, heldAt{};
        void* const pad = First(kPadClass);
        void* const held = Route() == "grab" ? Obj(me, L"grabbing_actor") : Obj(me, L"holding_actor");
        if (pad) E::TryGetActorLocation(pad, padAt);
        if (held) E::TryGetActorLocation(held, heldAt);
        ue_wrap::FVector meAt{};
        if (me) E::TryGetActorLocation(me, meAt);
        void* const pc = me ? E::GetController(me) : nullptr;
        const ue_wrap::FRotator view = pc ? E::GetControlRotation(pc) : ue_wrap::FRotator{};
        UE_LOGI("[COOKPAD] client: the player at (%.1f, %.1f, %.1f) looking pitch %.1f yaw %.1f; the pad at (%.1f, %.1f, %.1f)",
                meAt.X, meAt.Y, meAt.Z, view.Pitch, view.Yaw, padAt.X, padAt.Y, padAt.Z);
        UE_LOGI("[COOKPAD] client: the held steak at (%.1f, %.1f, %.1f), %.1f uu over the pad, before the drop", heldAt.X,
                heldAt.Y, heldAt.Z, heldAt.Z - padAt.Z);
        bool ran = false;
        if (Route() == "grab") {
            // The use key's release: the game's own dropGrabObject.
            ran = me && R::CallFunction(me, R::FindDispatchFunctionCached(R::ClassOf(me), L"dropGrabObject"), nullptr);
        } else {
            // The drop key (place false: the player's own place) or a placing drop (place true: the trace's hit).
            ue_wrap::ParamFrame f(me ? R::FindDispatchFunctionCached(R::ClassOf(me), L"simulateDrop") : nullptr);
            ran = f.valid() && f.Set<bool>(L"dontCollect", true) && f.Set<bool>(L"place", Route() == "place") &&
                  ue_wrap::Call(me, f);
        }
        UE_LOGI("[COOKPAD] client: dropped what the hand holds by the %hs route (ran=%d)", Route().c_str(), ran ? 1 : 0);
    }
    if (t < 20000u || t > 70000u || Now() < g_reportAt) return;
    g_reportAt = Now() + 500;
    Report();
}

}  // namespace coop::dev::cookpad_probe
