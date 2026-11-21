// coop/dev/halluc_probe.cpp -- [dev] the hallucinations, seen from every peer. See the header.

#include "coop/dev/halluc_probe.h"

#include "coop/config/config.h"
#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace coop::dev::halluc_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace V = ue_wrap::vitals;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_HALLUC_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
bool g_acted = false, g_restored = false;
bool g_holdFood = false, g_holdSleep = false;

void* ReadObj(void* base, const wchar_t* field) {
    if (!base) return nullptr;
    const int32_t off = R::FindPropertyOffset(R::ClassOf(base), field);
    if (off < 0) return nullptr;
    void* o = nullptr;
    std::memcpy(&o, static_cast<uint8_t*>(base) + off, sizeof(o));
    return o && R::IsLive(o) ? o : nullptr;
}

std::vector<void*> Live(const wchar_t* cls) {
    std::vector<void*> out;
    for (void* o : R::FindObjectsByClass(cls))
        if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__")) out.push_back(o);
    return out;
}

void HallFood(int n) {
    void* gm = ue_wrap::world_singleton::Gamemode();
    void* fn = gm ? R::FindFunction(R::ClassOf(gm), L"addHallFood") : nullptr;
    V::Write(V::Field::Food, 0.f);
    int ran = 0;
    for (int i = 0; fn && i < n; ++i) {
        ue_wrap::ParamFrame f(fn);
        if (f.valid() && ue_wrap::Call(gm, f)) ++ran;
    }
    V::Write(V::Field::Food, 100.f);
    UE_LOGI("[HALLUC] host ran addHallFood %d time(s)", ran);
}

// The roaches by look: the master's components, by the name of the mesh each shows.
void RoachLooks(int& burgers, int& roaches) {
    burgers = roaches = 0;
    void* master = ReadObj(ue_wrap::world_singleton::Gamemode(), L"cockroachMaster");
    if (!master) return;
    const int32_t off = R::FindPropertyOffset(R::ClassOf(master), L"roaches");
    if (off < 0) return;
    struct Arr { void** data; int32_t num, max; };
    const Arr* a = reinterpret_cast<const Arr*>(static_cast<uint8_t*>(master) + off);
    for (int32_t i = 0; a->data && i < a->num; ++i) {
        void* comp = a->data[i];
        if (!comp || !R::IsLive(comp)) continue;
        void* mesh = ReadObj(comp, L"StaticMesh");
        const std::wstring n = mesh ? R::ToString(R::NameOf(mesh)) : L"";
        if (n == L"burger") ++burgers;
        else ++roaches;
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[HALLUC] probe armed slot=%d", slot);
    }
    const uint64_t t = Now() - g_startAt;
    if (!g_acted && host && t >= 40000u) {
        g_acted = true;
        HallFood(5);
        g_holdSleep = true;
        UE_LOGI("[HALLUC] host sleep held at 3: ok=%d", V::Write(V::Field::Sleep, 3.f) ? 1 : 0);
    }
    if (!g_acted && !host && slot == 1 && t >= 60000u) {
        g_acted = true;
        g_holdFood = g_holdSleep = true;
        UE_LOGI("[HALLUC] slot 1 food held at 5 and sleep at 3: ok=%d/%d", V::Write(V::Field::Food, 5.f) ? 1 : 0,
                V::Write(V::Field::Sleep, 3.f) ? 1 : 0);
    }
    if (g_holdFood && !g_restored) {
        float food = 0.f;
        if (V::Read(V::Field::Food, &food) && food > 5.5f) V::Write(V::Field::Food, 5.f);
    }
    // Low enough that the spawner rolls (it weighs 20 - sleep), above the faint, whose sleep the spawner skips.
    if (g_holdSleep && !g_restored) {
        float sleep = 0.f;
        if (V::Read(V::Field::Sleep, &sleep) && (sleep > 3.5f || sleep < 2.5f)) V::Write(V::Field::Sleep, 3.f);
    }
    if (!g_restored && t >= 180000u) {
        g_restored = true;
        g_holdFood = g_holdSleep = false;
        V::Write(V::Field::Food, 100.f);
        V::Write(V::Field::Sleep, 100.f);
        int gone = 0;
        for (void* o : Live(L"insomniac_C")) gone += ue_wrap::engine::DestroyActor(o) ? 1 : 0;
        for (void* o : Live(L"insomniac_Child_C")) gone += ue_wrap::engine::DestroyActor(o) ? 1 : 0;
        UE_LOGI("[HALLUC] slot %d: needs back to 100, %d own Insomniac(s) destroyed", slot, gone);
    }
    if (t < 30000u || t > 200000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    std::vector<void*> insom = Live(L"insomniac_C");
    for (void* o : Live(L"insomniac_Child_C")) insom.push_back(o);
    void* me = coop::players::Registry::Get().Local();
    ue_wrap::FVector mine{};
    if (me) ue_wrap::engine::TryGetActorLocation(me, mine);
    float nearest = -1.f;
    for (void* o : insom) {
        ue_wrap::FVector at{};
        if (!ue_wrap::engine::TryGetActorLocation(o, at)) continue;
        const float d = std::hypot(at.X - mine.X, at.Y - mine.Y, at.Z - mine.Z);
        if (nearest < 0.f || d < nearest) nearest = d;
    }
    int bound = 0;
    const std::vector<void*> burgers = Live(L"prop_burgerHallucinate_C");
    for (void* b : burgers)
        if (coop::element::Registry::Get().EidForActor(b) != coop::element::kInvalidId) ++bound;
    int lookBurger = 0, lookRoach = 0;
    RoachLooks(lookBurger, lookRoach);
    float food = -1.f, sleep = -1.f;
    V::Read(V::Field::Food, &food);
    V::Read(V::Field::Sleep, &sleep);
    UE_LOGI("[HALLUC] t=%llus slot=%d food=%.0f sleep=%.0f | insomniacs=%zu nearest=%.0f cm | hall burgers=%zu "
            "bound=%d | roaches burger-look=%d roach-look=%d", static_cast<unsigned long long>(t / 1000), slot, food,
            sleep, insom.size(), nearest, burgers.size(), bound, lookBurger, lookRoach);
}

}  // namespace coop::dev::halluc_probe
