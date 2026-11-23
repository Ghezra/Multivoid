// coop/dev/week6_drill.cpp -- [dev] the story's sixth week on three peers. See the header.

#include "coop/dev/week6_drill.h"

#include "coop/config/config.h"
#include "coop/creatures/served_player.h"
#include "coop/dev/event_trigger.h"
#include "coop/net/session.h"
#include "coop/session/teleport_client.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace coop::dev::week6_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Arm() {
    static const std::string v = coop::config::ReadEnv("VOTVCOOP_WEEK6_DRILL");
    return v;
}

coop::net::Session* g_session = nullptr;
uint64_t g_startAt = 0, g_nextLog = 0;
std::unordered_set<void*> g_shots, g_blasts;   // hole: the guards' shots and the explosions seen on this machine
int g_step = 0;
FVector g_boxAt{};

std::vector<void*> Live(const wchar_t* cls) {
    std::vector<void*> out;
    for (void* o : R::FindObjectsByClass(cls))
        if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) out.push_back(o);
    return out;
}

bool Fire(const char* name) {
    for (const auto& ev : coop::dev::event_trigger::Events())
        if (std::strcmp(ev.name, name) == 0) return coop::dev::event_trigger::Trigger(ev);
    return false;
}

int Bool(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(obj)[off] & mask) != 0 ? 1 : 0;
}

float Float(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    float v = -1.f;
    if (off >= 0) std::memcpy(&v, static_cast<const uint8_t*>(obj) + off, sizeof(v));
    return v;
}

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return nullptr;
    void* v = nullptr;
    std::memcpy(&v, static_cast<const uint8_t*>(obj) + off, sizeof(v));
    return v && R::IsLive(v) ? v : nullptr;
}

bool Run(void* obj, const wchar_t* fnName) {
    void* const fn = obj ? R::FindDispatchFunction(R::ClassOf(obj), fnName, nullptr) : nullptr;
    ue_wrap::ParamFrame f(fn);
    return f.valid() && ue_wrap::Call(obj, f);
}

const char* RowOf(const std::string& arm) {
    if (arm == "borg") return "borgRozital";
    if (arm == "morning") return "morningGay";
    if (arm == "dropcar") return "fallcar_0";
    if (arm == "dropbody") return "fallbody_0";
    if (arm == "hole") return "rozitalHole";
    if (arm == "droptank") return "graystank";
    if (arm == "grays") return "graysforest";
    return nullptr;
}

std::vector<void*> Droppers() {
    std::vector<void*> d = Live(L"ufoDropper_car_C");
    for (const wchar_t* k : {L"ufoDropper_body_C", L"ufoDropper_tank_C", L"ufoDropper_pig_C"})
        for (void* o : Live(k)) d.push_back(o);
    return d;
}

void Act(bool host, int slot, uint64_t t) {
    const std::string& arm = Arm();
    void* const me = coop::players::Registry::Get().Local();
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        UE_LOGI("[WEEK6] host: the %s row fired (ok=%d)", RowOf(arm), Fire(RowOf(arm)) ? 1 : 0);
    }
    if (arm == "morning") {
        const std::vector<void*> ufos = Live(L"morningUfo_C");
        if (host && g_step == 1 && t >= 40000u && me && !ufos.empty()) {
            g_step = 2;
            g_boxAt = E::GetComponentLocation(Field(ufos[0], L"Box"));
            UE_LOGI("[WEEK6] host: into the UFO's box at (%.0f,%.0f,%.0f) (ok=%d)", g_boxAt.X, g_boxAt.Y, g_boxAt.Z,
                    E::SetActorLocation(me, g_boxAt) ? 1 : 0);
        }
        if (host && g_step == 2 && t >= 46000u && me) {
            g_step = 3;
            const FVector off{g_boxAt.X + 6000.f, g_boxAt.Y, g_boxAt.Z + 100.f};
            UE_LOGI("[WEEK6] host: 60 m off (ok=%d)", E::SetActorLocation(me, off) ? 1 : 0);
        }
        if (!host && slot == 1 && g_step == 0 && t >= 46000u && me && !ufos.empty()) {
            g_step = 1;
            const FVector at = E::GetComponentLocation(Field(ufos[0], L"Box"));
            UE_LOGI("[WEEK6] slot 1: into the UFO's box at (%.0f,%.0f,%.0f) (ok=%d)", at.X, at.Y, at.Z,
                    E::SetActorLocation(me, at) ? 1 : 0);
        }
    }
    if (arm == "hole") {
        const std::vector<void*> ctl = Live(L"event_bottomHoleController_C");
        if (host && g_step == 1 && t >= 40000u && me && !ctl.empty()) {
            g_step = 2;
            g_boxAt = E::GetComponentLocation(Field(ctl[0], L"Box"));
            UE_LOGI("[WEEK6] host: into the hole's box at (%.0f,%.0f,%.0f) (ok=%d)", g_boxAt.X, g_boxAt.Y, g_boxAt.Z,
                    E::SetActorLocation(me, g_boxAt) ? 1 : 0);
        }
        const std::vector<void*> dev = Live(L"holeDevice_C");
        // VOTVCOOP_WEEK6_HOLE_OUT=host: the host stands there instead of slot 1 (whether the guards shoot at all).
        static const bool s_hostOut = coop::config::ReadEnv("VOTVCOOP_WEEK6_HOLE_OUT") == "host";
        // The host moves the body: its own, or slot 1's by its order (a client's own jump is not a move the host takes).
        if (host && g_step == 2 && t >= 75000u && me && !dev.empty()) {
            if (s_hostOut) g_step = 4;
            static bool s_ordered = false;
            FVector at{};
            E::TryGetActorLocation(dev[0], at);
            const FVector to{at.X + 7000.f, at.Y, at.Z + 300.f};
            if (s_hostOut) {
                UE_LOGI("[WEEK6] host: 70 m from the device at (%.0f,%.0f,%.0f) (ok=%d)", to.X, to.Y, to.Z,
                        E::SetActorLocation(me, to) ? 1 : 0);
            } else if (!s_ordered) {
                s_ordered = true;
                UE_LOGI("[WEEK6] host: slot 1 ordered 70 m from the device at (%.0f,%.0f,%.0f) (ok=%d)", to.X, to.Y, to.Z,
                        g_session && coop::teleport_client::OrderToPose(*g_session, 1, to, ue_wrap::FRotator{}) ? 1 : 0);
            }
        }
        const std::vector<void*> ig = Live(L"igetis_C");
        if (host && g_step == 2 && t >= 190000u && me && !ig.empty()) {
            g_step = 3;
            FVector at{};
            E::TryGetActorLocation(ig[0], at);
            const FVector to{at.X + 300.f, at.Y, at.Z + 100.f};
            UE_LOGI("[WEEK6] host: 3 m from the Igetis at (%.0f,%.0f,%.0f) (ok=%d)", to.X, to.Y, to.Z,
                    E::SetActorLocation(me, to) ? 1 : 0);
        }
    }
    // grays: the transformer back on at 40 s (the controller's turnedOn mints the Grays); slot 1 ordered onto the first.
    if (arm == "grays" && host) {
        const std::vector<void*> ctl = Live(L"grayEventController_C");
        if (g_step == 1 && t >= 40000u && !ctl.empty()) {
            g_step = 2;
            const bool on = Run(ctl[0], L"turnedOn");
            UE_LOGI("[WEEK6] host: the Grays' controller turned on (ran=%d) and spawns them (ran=%d)", on ? 1 : 0,
                    Run(ctl[0], L"Spawn") ? 1 : 0);
        }
        const std::vector<void*> grays = Live(L"grayTest_C");
        FVector at{};
        if (g_step == 2 && t >= 55000u && !grays.empty() && g_session && E::TryGetActorLocation(grays[0], at)) {
            g_step = 3;
            UE_LOGI("[WEEK6] host: slot 1 ordered onto a Gray at (%.0f,%.0f,%.0f) (ok=%d)", at.X, at.Y, at.Z,
                    coop::teleport_client::OrderToPose(*g_session, 1, at, ue_wrap::FRotator{}) ? 1 : 0);
        }
    }
    // droptank: once the host's Firetank stands, the host orders slot 1 8 m from it.
    if (arm == "droptank" && host && g_step == 2 && g_session) {
        const std::vector<void*> ft = Live(L"firetank_C");
        FVector at{};
        if (!ft.empty() && E::TryGetActorLocation(ft[0], at)) {
            g_step = 3;
            const FVector to{at.X + 800.f, at.Y, at.Z + 200.f};
            UE_LOGI("[WEEK6] host: slot 1 ordered 8 m from the Firetank at (%.0f,%.0f,%.0f) (ok=%d)", to.X, to.Y, to.Z,
                    coop::teleport_client::OrderToPose(*g_session, 1, to, ue_wrap::FRotator{}) ? 1 : 0);
        }
    }
    if ((arm == "dropcar" || arm == "dropbody" || arm == "droptank") && host && g_step == 1 && t >= 40000u) {
        const std::vector<void*> d = Droppers();
        if (!d.empty()) {
            g_step = 2;
            UE_LOGI("[WEEK6] host: the saucer's forceDeploy (ran=%d)", Run(d[0], L"forceDeploy") ? 1 : 0);
        }
    }
}

void Report(bool host, uint64_t t) {
    if (t < 25000u || t > (Arm() == "hole" || Arm() == "droptank" || Arm() == "grays" ? 240000u : 150000u) ||
        Now() < g_nextLog)
        return;
    g_nextLog = Now() + 3000u;
    const std::string& arm = Arm();
    const char* const who = host ? "host" : "client";
    if (arm == "borg") {
        const std::vector<void*> b = Live(L"rozitBorg_C");
        const FVector ships = b.empty() ? FVector{} : E::GetComponentLocation(Field(b[0], L"ships"));
        UE_LOGI("[WEEK6] borg %s t=%llu: motherships %zu, alpha %.4f, ships at (%.0f,%.0f,%.0f)", who,
                static_cast<unsigned long long>(t / 1000u), b.size(), b.empty() ? -1.f : Float(b[0], L"alpha"),
                ships.X, ships.Y, ships.Z);
    } else if (arm == "hole") {
        static const wchar_t* const kParts[] = {L"event_bottomHoleController_C", L"holeBottom_C", L"holeDevice_C",
                                                L"kavotiaPatrolController_C", L"kavotia_C", L"kavotiaShot_C",
                                                L"igetis_C", L"prop_rozitalBeacon_C", L"prop_holeDeviceCapsule_C"};
        std::wstring line;
        for (const wchar_t* k : kParts) line += std::wstring(k) + L"=" + std::to_wstring(Live(k).size()) + L" ";
        void* const gm = ue_wrap::world_singleton::Gamemode();
        void* const slotObj = gm ? Field(gm, L"saveSlot") : nullptr;
        // The save's grate and fence, and the grate prop the Hole's open makes, by their prop names.
        for (void* pr : Live(L"prop_C")) {
            const int32_t off = R::FindPropertyOffset(R::ClassOf(pr), L"Name");
            if (off < 0) continue;
            const std::wstring nm = R::ToString(*reinterpret_cast<const R::FName*>(static_cast<uint8_t*>(pr) + off));
            if (nm.rfind(L"hole", 0) != 0) continue;
            FVector at{};
            E::TryGetActorLocation(pr, at);
            line += nm + L"@(" + std::to_wstring(static_cast<int>(at.X)) + L"," + std::to_wstring(static_cast<int>(at.Y)) +
                    L"," + std::to_wstring(static_cast<int>(at.Z)) + L") ";
        }
        UE_LOGI("[WEEK6] hole %s t=%llu: %lsshots seen %zu, explosions seen %zu, health %.0f", who,
                static_cast<unsigned long long>(t / 1000u), line.c_str(), g_shots.size(), g_blasts.size(),
                slotObj ? Float(slotObj, L"health") : -1.f);
    } else if (arm == "morning") {
        const std::vector<void*> u = Live(L"morningUfo_C");
        FVector at{};
        if (!u.empty()) E::TryGetActorLocation(u[0], at);
        UE_LOGI("[WEEK6] morning %s t=%llu: UFOs %zu, hidden %d, fly %d, flyAway %d, looks at slot %d, at "
                "(%.0f,%.0f,%.0f)", who, static_cast<unsigned long long>(t / 1000u), u.size(),
                u.empty() ? -1 : Bool(u[0], L"bHidden"), u.empty() ? -1 : Bool(u[0], L"fly"),
                u.empty() ? -1 : Bool(u[0], L"flyAway"),
                u.empty() || !host ? -1 : static_cast<int>(coop::served_player::ServedSlot(u[0])), at.X, at.Y, at.Z);
    } else {
        const std::vector<void*> d = Droppers();
        void* const s = d.empty() ? nullptr : d[0];
        void* const dropCls = s ? Field(s, L"drop") : nullptr;
        const std::wstring dropName = dropCls ? R::ToString(R::NameOf(dropCls)) : L"-";
        size_t drops = 0;
        if (dropCls) drops = Live(dropName.c_str()).size();
        const FVector body = s ? E::GetComponentRelativeLocation(Field(s, L"grayUfoSk")) : FVector{};
        std::wstring more;
        if (arm == "grays") {
            void* const me = coop::players::Registry::Get().Local();
            void* const car = Field(ue_wrap::world_singleton::Gamemode(), L"car");
            more += L", grays=" + std::to_wstring(Live(L"grayTest_C").size()) + L", ragdoll " +
                    std::to_wstring(Bool(me, L"isRagdoll")) + L", atv zapped " + std::to_wstring(Bool(car, L"zapped"));
        }
        if (arm == "droptank" || arm == "grays") {
            for (const wchar_t* k : {L"firetankDeploy_C", L"firetank_C", L"grayboar_C"})
                more += std::wstring(L", ") + k + L"=" + std::to_wstring(Live(k).size());
            const std::vector<void*> ft = Live(L"firetank_C");
            FVector at{};
            if (!ft.empty() && E::TryGetActorLocation(ft[0], at))
                more += L" at (" + std::to_wstring(static_cast<int>(at.X)) + L"," + std::to_wstring(static_cast<int>(at.Y)) +
                        L")";
            void* const gm = ue_wrap::world_singleton::Gamemode();
            void* const slotObj = gm ? Field(gm, L"saveSlot") : nullptr;
            more += L", health " + std::to_wstring(static_cast<int>(slotObj ? Float(slotObj, L"health") : -1.f));
        }
        UE_LOGI("[WEEK6] %s %s t=%llu: saucers %zu, hidden %d, body z %.0f, weight %.2f, drops of %ls %zu%ls", arm.c_str(),
                who, static_cast<unsigned long long>(t / 1000u), d.size(), s ? Bool(s, L"bHidden") : -1, body.Z,
                s ? Float(s, L"weight") : -1.f, dropName.c_str(), drops, more.c_str());
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Arm().empty() || !RowOf(Arm()) || !session || !session->running() || !session->connected()) return;
    g_session = session;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[WEEK6] drill armed arm=%s slot=%d", Arm().c_str(), slot);
    }
    const uint64_t t = Now() - g_startAt;
    Act(host, slot, t);
    if (Arm() == "hole") {
        for (void* o : Live(L"kavotiaShot_C")) g_shots.insert(o);
        for (void* o : Live(L"explosion_C")) g_blasts.insert(o);
    }
    Report(host, t);
    static bool s_done = false;
    if (!s_done && t >= (Arm() == "hole" || Arm() == "droptank" || Arm() == "grays" ? 242000u : 152000u)) {
        s_done = true;
        UE_LOGI("[WEEK6] done arm=%s slot=%d", Arm().c_str(), slot);
    }
}

}  // namespace coop::dev::week6_drill
