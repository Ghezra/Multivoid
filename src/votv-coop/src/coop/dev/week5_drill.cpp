// coop/dev/week5_drill.cpp -- [dev] the story's fifth week on three peers. See the header.

#include "coop/dev/week5_drill.h"

#include "coop/config/config.h"
#include "coop/creatures/served_player.h"
#include "coop/dev/event_trigger.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_component.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace coop::dev::week5_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Arm() {
    static const std::string v = coop::config::ReadEnv("VOTVCOOP_WEEK5_DRILL");
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;
int g_pinFrames = 0;

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

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return nullptr;
    void* v = nullptr;
    std::memcpy(&v, static_cast<const uint8_t*>(obj) + off, sizeof(v));
    return v && R::IsLive(v) ? v : nullptr;
}

float Dist(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The nearest scout to `at`, with its place.
void* Nearest(const std::vector<void*>& balls, const FVector& at, FVector& where, float& d) {
    void* best = nullptr;
    d = 1e30f;
    for (void* b : balls) {
        FVector p{};
        if (!E::TryGetActorLocation(b, p)) continue;
        const float x = Dist(p, at);
        if (x < d) {
            d = x;
            best = b;
            where = p;
        }
    }
    return best;
}

void TickScouts(bool host, int slot, uint64_t t) {
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        UE_LOGI("[WEEK5] host: the scouts' row fired (ok=%d)", Fire("tentacleBalls") ? 1 : 0);
    }
    void* const me = coop::players::Registry::Get().Local();
    const std::vector<void*> balls = Live(L"tentacleBall_C");
    // Slot 1 to 15 m off the first scout, at eight bearings, each kept when the player still stands near it 2 s later
    // (inside a wall the game's clip check sends the body below the world).
    static int s_spot = 0;
    static uint64_t s_movedAt = 0;
    static FVector s_to{};
    if (!host && slot == 1 && g_step < 2 && t >= 45000u && !balls.empty() && me) {
        if (g_step == 1 && Now() - s_movedAt >= 2000u) {
            FVector mine{};
            E::TryGetActorLocation(me, mine);
            if (Dist(mine, s_to) < 400.f) {
                g_step = 2;
                UE_LOGI("[WEEK5] slot 1: its player holds at (%.0f,%.0f,%.0f)", mine.X, mine.Y, mine.Z);
                return;
            }
            g_step = 0;
            if (++s_spot >= 8) {
                g_step = 3;
                UE_LOGW("[WEEK5] slot 1: no spot near the scouts held its player");
                return;
            }
        }
        if (g_step == 0) {
            FVector at{};
            E::TryGetActorLocation(balls[0], at);
            const float a = static_cast<float>(s_spot) * 0.785398f;
            s_to = FVector{at.X + 1500.f * std::cos(a), at.Y + 1500.f * std::sin(a), at.Z + 150.f};
            g_step = 1;
            s_movedAt = Now();
            UE_LOGI("[WEEK5] slot 1: to spot %d by the scouts (ok=%d)", s_spot, E::SetActorLocation(me, s_to) ? 1 : 0);
        }
    }
    // This peer's own player in a first-person body animation, the pin's: counted each frame it holds.
    if (me && Bool(me, L"isInFPBAnimation") == 1 && ++g_pinFrames == 1)
        UE_LOGI("[WEEK5] %s: its own player is pinned", host ? "host" : "client");
    if (t < 25000u || t > 240000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 3000u;
    int hostile = 0, chasing = 0, anim = 0, served = 0;
    for (void* b : balls) {
        hostile += Bool(b, L"isHostile") == 1;
        chasing += Bool(b, L"chasing") == 1;
        anim += Bool(b, L"isAnim") == 1;
        served += host && coop::served_player::ServedSlot(b) != 0;
    }
    FVector mine{}, p1{}, where{};
    float dMe = -1.f, d1 = -1.f;
    if (me && E::TryGetActorLocation(me, mine)) Nearest(balls, mine, where, dMe);
    if (host) {
        coop::RemotePlayer* const rp = coop::players::Registry::Get().Puppet(1);
        if (rp && rp->TryGetLocation(p1)) Nearest(balls, p1, where, d1);
    }
    UE_LOGI("[WEEK5] scouts t=%llus: %zu scout(s), %d hostile, %d chasing, %d pinning, %d hunting a client; nearest "
            "to this player %.0f cm%s, slot 1 %.0f cm; pinned frames here %d",
            static_cast<unsigned long long>(t / 1000), balls.size(), hostile, chasing, anim, served, dMe,
            host ? "" : " (own)", d1, g_pinFrames);
}

// The garage Soltomia: the host fires its row, walks its own player into its box at 40 s and runs its opened at 70 s.
void TickSolto(bool host, uint64_t t) {
    const std::vector<void*> solts = Live(L"soltomiaCleaning_C");
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        UE_LOGI("[WEEK5] host: the Soltomia's row fired (ok=%d)", Fire("soltoClean") ? 1 : 0);
    }
    void* const me = coop::players::Registry::Get().Local();
    // Into its box: to the box's own place, and 5 s later, if the box has not run, its overlap handler with this player,
    // as coop/dev/event_force drives a story box.
    void* const box = solts.empty() ? nullptr : Field(solts[0], L"Box");
    if (host && g_step == 1 && t >= 40000u && box && me) {
        g_step = 2;
        const FVector at = ue_wrap::engine::GetComponentLocation(box);
        UE_LOGI("[WEEK5] host: its player into the Soltomia's box at (%.0f,%.0f,%.0f) (ok=%d)", at.X, at.Y, at.Z,
                E::SetActorLocation(me, at) ? 1 : 0);
    }
    static bool s_drove = false;
    if (host && g_step == 2 && !s_drove && t >= 45000u && box && me && Bool(solts[0], L"active") != 1) {
        s_drove = true;
        void* const fn = R::FindFunction(R::ClassOf(solts[0]),
                                         L"BndEvt__soltomiaCleaning_Box_K2Node_ComponentBoundEvent_0_"
                                         L"ComponentBeginOverlapSignature__DelegateSignature");
        ue_wrap::ParamFrame f(fn);
        bool ran = false;
        if (f.valid()) {
            f.Set<void*>(L"OverlappedComponent", box);
            f.Set<void*>(L"OtherActor", me);
            ran = ue_wrap::Call(solts[0], f);
        }
        UE_LOGI("[WEEK5] host: the box had not run; its handler with this player (ran=%d, active=%d)", ran ? 1 : 0,
                Bool(solts[0], L"active"));
    }
    if (host && g_step == 2 && t >= 70000u && !solts.empty()) {
        g_step = 3;
        void* const fn = R::FindFunction(R::ClassOf(solts[0]), L"opened");
        ue_wrap::ParamFrame f(fn);
        UE_LOGI("[WEEK5] host: the Soltomia's opened run (ok=%d)", f.valid() && ue_wrap::Call(solts[0], f) ? 1 : 0);
    }
    if (t < 25000u || t > 150000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 3000u;
    int shown = 0;
    for (void* o : solts) shown += Bool(o, L"bHidden") == 0;
    UE_LOGI("[WEEK5] solto t=%llus: %zu Soltomia(s), %d shown, %zu Soltomia sponge(s)",
            static_cast<unsigned long long>(t / 1000), solts.size(), shown, Live(L"prop_sponge_solto_C").size());
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Arm().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[WEEK5] drill armed slot=%d arm=%s", slot, Arm().c_str());
    }
    const uint64_t t = Now() - g_startAt;
    if (Arm() == "scouts") TickScouts(host, slot, t);
    else if (Arm() == "solto") TickSolto(host, t);
}

}  // namespace coop::dev::week5_drill
