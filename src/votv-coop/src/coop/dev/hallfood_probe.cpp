// coop/dev/hallfood_probe.cpp -- [dev] a food hallucination on the host and on slot 1, counted on every peer. See the
// header.

#include "coop/dev/hallfood_probe.h"

#include "coop/config/config.h"
#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace coop::dev::hallfood_probe {
namespace {

namespace R = ue_wrap::reflection;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_HALLFOOD_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0, g_nextLog = 0;
bool g_called = false;

void CallHallFood(const char* who) {
    void* const gm = ue_wrap::world_singleton::Gamemode();
    void* const fn = gm ? R::FindFunction(R::ClassOf(gm), L"addHallFood") : nullptr;
    int ok = 0;
    for (int i = 0; fn && i < 5; ++i) {
        ue_wrap::ParamFrame f(fn);
        if (f.valid() && ue_wrap::Call(gm, f)) ++ok;
    }
    UE_LOGI("[HALLFOOD] %s calls addHallFood five times: gamemode=%p fn=%p ran=%d", who, gm, fn, ok);
}

struct Census {
    int live = 0, bound = 0;
    std::string where;
};

void Visit(void* ctx, void* obj, int32_t /*index*/) {
    auto* c = static_cast<Census*>(ctx);
    ++c->live;
    const auto eid = coop::element::Registry::Get().EidForActor(obj);
    if (eid != coop::element::kInvalidId) ++c->bound;
    ue_wrap::FVector at{};
    if (c->live <= 6 && ue_wrap::engine::TryGetActorLocation(obj, at)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " (%.0f,%.0f,%.0f eid=%u)", at.X, at.Y, at.Z, static_cast<unsigned>(eid));
        c->where += buf;
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[HALLFOOD] probe armed slot=%d", slot);
    }
    const uint64_t t = Now() - g_startAt;
    if (!g_called && host && t >= 40000u) { g_called = true; CallHallFood("the host"); }
    if (!g_called && !host && slot == 1 && t >= 70000u) { g_called = true; CallHallFood("slot 1"); }
    if (t < 30000u || t > 150000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 5000u;
    Census c;
    void* const cls = ue_wrap::object_index::ClassByName(L"prop_burgerHallucinate_C");
    if (cls) ue_wrap::object_index::ForEachInstance(cls, &Visit, &c);
    UE_LOGI("[HALLFOOD] t=%llus slot=%d class=%d live=%d bound=%d%s", static_cast<unsigned long long>(t / 1000), slot,
            cls ? 1 : 0, c.live, c.bound, c.where.c_str());
}

}  // namespace coop::dev::hallfood_probe
