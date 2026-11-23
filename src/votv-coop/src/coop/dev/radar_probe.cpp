// coop/dev/radar_probe.cpp -- [dev] the radar's dots on every peer (VOTVCOOP_RADAR_PROBE=1 on every peer). The radar
// draws mainGamemode_C.radarObjects, which a comp_radarPoint_C adds its owner to at its begin play and takes it from at
// its destroy. Times are from each peer's own connect.
//   HOST: 50 s the obelisk's row, 70 s the radio tower ship's, 100 s the Hole Repair's (its player into the box).
//   Every peer, each 5 s to 240 s (`[RADAR]`): the list's count, then each class's count and its points, to 10 m.

#include "coop/dev/radar_probe.h"

#include "coop/config/config.h"
#include "coop/dev/event_trigger.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace coop::dev::radar_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

bool Armed() {
    static const bool v = !coop::config::ReadEnv("VOTVCOOP_RADAR_PROBE").empty();
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;

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

// mainGamemode_C.radarObjects: a TArray<AActor*>.
std::vector<void*> RadarObjects() {
    std::vector<void*> out;
    void* const gm = ue_wrap::world_singleton::Gamemode();
    const int32_t off = gm ? R::FindPropertyOffset(R::ClassOf(gm), L"radarObjects") : -1;
    if (off < 0) return out;
    void* data = nullptr;
    int32_t num = 0;
    std::memcpy(&data, static_cast<const uint8_t*>(gm) + off, sizeof(data));
    std::memcpy(&num, static_cast<const uint8_t*>(gm) + off + sizeof(void*), sizeof(num));
    if (!data || num < 0 || num > 4096) return out;
    for (int32_t i = 0; i < num; ++i) out.push_back(static_cast<void**>(data)[i]);
    return out;
}

void Report(bool host, uint64_t t) {
    const std::vector<void*> list = RadarObjects();
    std::map<std::wstring, std::vector<std::wstring>> byClass;
    int dead = 0;
    for (void* a : list) {
        if (!a || !R::IsLive(a)) {
            ++dead;
            continue;
        }
        ue_wrap::FVector at{};
        E::TryGetActorLocation(a, at);
        byClass[R::ClassNameOf(a)].push_back(L"(" + std::to_wstring(static_cast<int>(at.X / 1000.f)) + L"," +
                                             std::to_wstring(static_cast<int>(at.Y / 1000.f)) + L")");
    }
    std::wstring line;
    for (auto& kv : byClass) {
        std::sort(kv.second.begin(), kv.second.end());
        line += kv.first + L"=" + std::to_wstring(kv.second.size());
        for (const auto& p : kv.second) line += p;
        line += L" ";
    }
    UE_LOGI("[RADAR] %s t=%llu: n=%zu dead=%d %ls", host ? "host" : "client", static_cast<unsigned long long>(t / 1000u),
            list.size(), dead, line.c_str());
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!Armed() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const uint64_t now = ::GetTickCount64();
    if (!g_startAt) {
        g_startAt = now;
        UE_LOGI("[RADAR] probe armed");
    }
    const uint64_t t = now - g_startAt;
    if (host) {
        if (g_step == 0 && t >= 50000u) {
            g_step = 1;
            UE_LOGI("[RADAR] host: obelisk fired (ok=%d)", Fire("obelisk") ? 1 : 0);
        }
        if (g_step == 1 && t >= 70000u) {
            g_step = 2;
            UE_LOGI("[RADAR] host: arirShip fired (ok=%d)", Fire("arirShip") ? 1 : 0);
        }
        if (g_step == 2 && t >= 100000u) {
            g_step = 3;
            UE_LOGI("[RADAR] host: rozitalHole fired (ok=%d)", Fire("rozitalHole") ? 1 : 0);
        }
        if (g_step == 3 && t >= 110000u) {
            g_step = 4;
            void* const me = coop::players::Registry::Get().Local();
            for (void* o : R::FindObjectsByClass(L"event_bottomHoleController_C"))
                if (o && R::IsLive(o) && me && !R::NameStartsWith(R::NameOf(o), L"Default__")) {
                    E::SetActorLocation(me, E::GetComponentLocation(Field(o, L"Box")));
                    UE_LOGI("[RADAR] host: into the Hole's box");
                    break;
                }
        }
    }
    if (t >= 25000u && t <= 240000u && now >= g_nextLog) {
        g_nextLog = now + 5000u;
        Report(host, t);
    }
    static bool s_done = false;
    if (!s_done && t >= 242000u) {
        s_done = true;
        UE_LOGI("[RADAR] done");
    }
}

}  // namespace coop::dev::radar_probe
