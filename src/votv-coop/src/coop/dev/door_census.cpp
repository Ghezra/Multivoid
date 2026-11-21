// coop/dev/door_census.cpp -- [dev] every door's state on every peer (VOTVCOOP_DOOR_CENSUS=1).
//
// The card: the satellites' doors and some of the base's could not be opened. EVERY PEER, 30 s and 60 s after its own
// connect: one line per door_C, sorted by name, with its place, its open flag, its power (active), whether it ignores a
// blackout, and its jammed, locked and autoclose bools where the class has them (`[DOOR-CENSUS]`); a line per peer
// totals them. Diffing the host's lines against a client's names any door whose state differs between them.

#include "coop/dev/door_census.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/devices/door.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace coop::dev::door_census {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;
namespace D  = ue_wrap::door;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_DOOR_CENSUS") == "1";
    return on;
}

// A Blueprint bool by name: 1 or 0, or -1 when the class has none.
int Bool(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!obj || !R::FindBoolProperty(R::ClassOf(obj), name, off, mask)) return -1;
    return (static_cast<const uint8_t*>(obj)[off] & mask) ? 1 : 0;
}

void Census(bool host) {
    std::vector<void*> doors;
    if (void* const cls = OI::ClassByName(L"door_C"))
        OI::ForEachInstanceOfKind(cls, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &doors);
    std::sort(doors.begin(), doors.end(),
              [](void* a, void* b) { return R::ToString(R::NameOf(a)) < R::ToString(R::NameOf(b)); });
    int open = 0, unpowered = 0, jammed = 0, locked = 0;
    for (void* d : doors) {
        ue_wrap::FVector at{};
        E::TryGetActorLocation(d, at);
        bool o = false, act = false, ign = false;
        const bool haveO = D::TryReadOpen(d, o), haveA = D::TryReadActive(d, act), haveI = D::TryReadIgnoresBlackout(d, ign);
        const int jam = Bool(d, L"jammed"), lock = Bool(d, L"locked"), autoclose = Bool(d, L"autoclose");
        open += haveO && o;
        unpowered += haveA && !act;
        jammed += jam == 1;
        locked += lock == 1;
        UE_LOGI("[DOOR-CENSUS] %s %ls %ls at (%.0f,%.0f,%.0f): open %d, active %d, ignoreBlackout %d, jammed %d, locked %d, "
                "autoclose %d",
                host ? "host" : "client", R::ToString(R::NameOf(d)).c_str(), R::ClassNameOf(d).c_str(), at.X, at.Y, at.Z,
                haveO ? (o ? 1 : 0) : -1, haveA ? (act ? 1 : 0) : -1, haveI ? (ign ? 1 : 0) : -1, jam, lock, autoclose);
    }
    UE_LOGI("[DOOR-CENSUS] %s total: %zu doors, %d open, %d unpowered, %d jammed, %d locked", host ? "host" : "client",
            doors.size(), open, unpowered, jammed, locked);
}

uint64_t g_startAt = 0;
int g_done = 0;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[DOOR-CENSUS] armed role=%s", host ? "host" : "client");
    }
    const uint64_t t = Now() - g_startAt;
    if ((g_done == 0 && t >= 30000u) || (g_done == 1 && t >= 60000u)) {
        ++g_done;
        Census(host);
    }
}

}  // namespace coop::dev::door_census
