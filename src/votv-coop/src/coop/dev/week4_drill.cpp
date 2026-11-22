// coop/dev/week4_drill.cpp -- [dev] the story's fourth week on three peers. See the header.

#include "coop/dev/week4_drill.h"

#include "coop/config/config.h"
#include "coop/dev/event_force.h"
#include "coop/dev/event_trigger.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/world/active_events.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace coop::dev::week4_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

const std::string& Arm() {
    static const std::string v = coop::config::ReadEnv("VOTVCOOP_WEEK4_DRILL");
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;

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

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return nullptr;
    void* v = nullptr;
    std::memcpy(&v, static_cast<const uint8_t*>(obj) + off, sizeof(v));
    return v && R::IsLive(v) ? v : nullptr;
}

int Bool(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(obj)[off] & mask) != 0 ? 1 : 0;
}

int Rocks() {
    int n = 0;
    for (void* o : Live(L"prop_C")) {
        if (R::ClassNameOf(o) != L"prop_C") continue;
        const int32_t off = R::FindPropertyOffset(R::ClassOf(o), L"name");
        if (off < 0) continue;
        if (R::ToString(*reinterpret_cast<const R::FName*>(static_cast<const uint8_t*>(o) + off)) == L"rock") ++n;
    }
    return n;
}

void TickObelisk(bool host, uint64_t t) {
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        UE_LOGI("[WEEK4] host: the warning obelisk forced through its box (ok=%d)",
                coop::dev::event_force::ForceNow("obelisk") ? 1 : 0);
    }
    if (t < 25000u || t > 120000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 2000u;
    const std::vector<void*> obs = Live(L"obelisk_C");
    float meshZ = 0.f;
    int fallen = -1;
    if (!obs.empty()) {
        fallen = Bool(obs[0], L"fallen");
        if (void* mesh = Field(obs[0], L"StaticMesh")) meshZ = ue_wrap::engine::GetComponentLocation(mesh).Z;
    }
    int32_t events = -1;
    if (ue_wrap::active_events::EnsureResolved()) ue_wrap::active_events::ReadCount(events);
    UE_LOGI("[WEEK4] obelisk t=%llus: %zu obelisk(s) fallen=%d mesh z %.0f, %d rock(s), %zu standing, %d event(s) here",
            static_cast<unsigned long long>(t / 1000), obs.size(), fallen, meshZ, Rocks(),
            Live(L"prop_obelisk_C").size(), static_cast<int>(events));
}

void TickGun(bool host, int slot, uint64_t t) {
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        UE_LOGI("[WEEK4] host: the dropped gun's row fired (ok=%d)", Fire("toeStab") ? 1 : 0);
    }
    // Slot 1's player walks into its own copy of the garage box, as a client's player does: the box's own overlap
    // handler with this player's pawn, as coop/dev/event_force drives the host's box.
    if (!host && slot == 1 && g_step == 0 && t >= 45000u) {
        g_step = 1;
        void* box = nullptr;
        for (void* o : Live(L"trigger_box_N_C"))
            if (R::NameStartsWith(R::NameOf(o), L"TB_event_arirToe")) box = o;
        void* const me = coop::players::Registry::Get().Local();
        void* const fn = box ? R::FindFunction(R::ClassOf(box),
                                               L"BndEvt__Box_K2Node_ComponentBoundEvent_0_"
                                               L"ComponentBeginOverlapSignature__DelegateSignature")
                             : nullptr;
        ue_wrap::ParamFrame f(fn);
        bool ran = false;
        if (f.valid() && me) {
            f.Set<void*>(L"OverlappedComponent", Field(box, L"Box"));
            f.Set<void*>(L"OtherActor", me);
            ran = ue_wrap::Call(box, f);
        }
        UE_LOGI("[WEEK4] slot 1: its player into its garage box (box=%d ran=%d)", box ? 1 : 0, ran ? 1 : 0);
    }
    if (t < 25000u || t > 120000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    UE_LOGI("[WEEK4] gun t=%llus: %zu gun(s), %zu gun stealer(s)", static_cast<unsigned long long>(t / 1000),
            Live(L"prop_arirGun_C").size(), Live(L"npc_arirGunStealer_C").size());
}

// Whether the host's own copy of an armed event box fires for a client's player standing in it: the host arms the row
// (VOTVCOOP_WEEK4_BOX=<row>|<box name>); slot 1 stands in the box's middle; the host logs the box's count, N.
void TickBox(bool host, int slot, uint64_t t) {
    static const std::string spec = coop::config::ReadEnv("VOTVCOOP_WEEK4_BOX");
    const size_t bar = spec.find('|');
    if (bar == std::string::npos) return;
    const std::string row = spec.substr(0, bar);
    const std::wstring name(spec.begin() + static_cast<std::ptrdiff_t>(bar) + 1, spec.end());
    void* box = nullptr;
    for (void* o : Live(L"trigger_box_N_C"))
        if (R::NameStartsWith(R::NameOf(o), name.c_str())) box = o;
    if (host && g_step == 0 && t >= 30000u) {
        g_step = 1;
        UE_LOGI("[WEEK4] host: the row '%s' fired, arming its box (ok=%d)", row.c_str(), Fire(row.c_str()) ? 1 : 0);
    }
    // Into the box: five spots round its middle, each kept only if the player still stands near it 2 s later (inside a
    // wall the game's clip check sends the body below the world).
    static float kSpots[5][2] = {{0.f, 0.f}, {120.f, 0.f}, {-120.f, 0.f}, {0.f, 120.f}, {0.f, -120.f}};
    static int s_spot = 0;
    static uint64_t s_movedAt = 0;
    void* const me = coop::players::Registry::Get().Local();
    if (!host && slot == 1 && g_step < 2 && t >= 45000u && box && me) {
        void* const comp = Field(box, L"Box");
        FVector at{}, mine{};
        if (!comp) E::TryGetActorLocation(box, at);
        else at = ue_wrap::engine::GetComponentLocation(comp);
        E::TryGetActorLocation(me, mine);
        if (g_step == 1 && Now() - s_movedAt >= 2000u) {
            if (comp && ue_wrap::engine::IsOverlappingActor(comp, me)) {
                g_step = 2;
                UE_LOGI("[WEEK4] slot 1: its player stands in %ls at (%.0f,%.0f,%.0f)", name.c_str(), mine.X, mine.Y,
                        mine.Z);
                return;
            }
            g_step = 0;
            if (++s_spot >= 5) {
                g_step = 3;
                UE_LOGW("[WEEK4] slot 1: no spot in %ls held its player", name.c_str());
                return;
            }
        }
        if (g_step == 0) {
            // The spots at half the box's own reach, so each stands inside it.
            static bool s_sized = false;
            const int32_t extOff = comp ? R::FindPropertyOffset(R::ClassOf(comp), L"BoxExtent") : -1;
            if (!s_sized && extOff >= 0) {
                s_sized = true;
                FVector ext{}, scale{1.f, 1.f, 1.f};
                std::memcpy(&ext, static_cast<const uint8_t*>(comp) + extOff, sizeof(ext));
                ue_wrap::engine::TryGetComponentWorldScale(comp, scale);
                const float hx = ext.X * scale.X * 0.5f, hy = ext.Y * scale.Y * 0.5f;
                const float spots[5][2] = {{hx, 0.f}, {-hx, 0.f}, {0.f, hy}, {0.f, -hy}, {0.f, 0.f}};
                std::memcpy(kSpots, spots, sizeof(spots));
                UE_LOGI("[WEEK4] slot 1: %ls reaches %.0f x %.0f x %.0f cm", name.c_str(), ext.X * scale.X * 2.f,
                        ext.Y * scale.Y * 2.f, ext.Z * scale.Z * 2.f);
            }
            g_step = 1;
            s_movedAt = Now();
            const FVector to{at.X + kSpots[s_spot][0], at.Y + kSpots[s_spot][1], at.Z};
            UE_LOGI("[WEEK4] slot 1: to spot %d in %ls (ok=%d)", s_spot, name.c_str(),
                    E::SetActorLocation(me, to) ? 1 : 0);
        }
    }
    if (t < 25000u || t > 120000u || Now() < g_nextLog || !box) return;
    g_nextLog = Now() + 5000u;
    const int32_t off = R::FindPropertyOffset(R::ClassOf(box), L"N");
    const int n = off >= 0 ? *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(box) + off) : -99;
    UE_LOGI("[WEEK4] box t=%llus: %ls N=%d, %zu Ariral gun(s) here", static_cast<unsigned long long>(t / 1000),
            name.c_str(), n, Live(L"prop_arirGun_C").size());
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Arm().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[WEEK4] drill armed slot=%d arm=%s", slot, Arm().c_str());
    }
    const uint64_t t = Now() - g_startAt;
    if (Arm() == "obelisk") TickObelisk(host, t);
    else if (Arm() == "gun") TickGun(host, slot, t);
    else if (Arm() == "box") TickBox(host, slot, t);
}

}  // namespace coop::dev::week4_drill
