// coop/dev/kdisc_watch.cpp -- [dev] where every floppy disc is (VOTVCOOP_KDISC_WATCH=1, on both peers), for
// Ghezra/Multivoid#38: a disc a client gave the Omega was seen stuck in the air.
//
// BOTH PEERS, from 15 s to 135 s after a client is connected, every 2 s: each live floppy disc whose place moved more
// than 5 cm, whose attach parent changed, or whose physics turned on or off since its last line is logged
// (`[KDISC]`) with its key, place, parent and physics. A disc gone since the last pass is said once.

#include "coop/dev/kdisc_watch.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace coop::dev::kdisc_watch {
namespace {

namespace R = ue_wrap::reflection;
namespace OI = ue_wrap::object_index;
namespace E = ue_wrap::engine;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = !coop::config::ReadEnv("VOTVCOOP_KDISC_WATCH").empty();
    return v;
}

struct Seen {
    ue_wrap::FVector at;
    void* parent;
    bool sim;
    bool live;
};

uint64_t g_startAt = 0;
uint64_t g_nextAt = 0;
std::unordered_map<void*, Seen> g_seen;

std::vector<void*> Discs() {
    std::vector<void*> classes, out;
    OI::ForEachClass([](void* ctx, void* cls, void*) { static_cast<std::vector<void*>*>(ctx)->push_back(cls); }, &classes);
    for (void* cls : classes) {
        if (!R::NameStartsWith(R::NameOf(cls), L"prop_floppyDisc")) continue;
        OI::ForEachInstance(cls, [](void* c, void* o, int32_t) {
            if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__"))
                static_cast<std::vector<void*>*>(c)->push_back(o);
        }, &out);
    }
    return out;
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    if (!g_startAt) g_startAt = Now();
    const uint64_t t = Now() - g_startAt;
    if (t < 15000u || t > 135000u || Now() < g_nextAt) return;
    g_nextAt = Now() + 2000;
    for (auto& [d, s] : g_seen) s.live = false;
    for (void* d : Discs()) {
        ue_wrap::FVector at{};
        if (!E::TryGetActorLocation(d, at)) continue;
        void* parent = nullptr;
        E::GetAttachParentActor(d, parent);
        const bool sim = E::IsActorRootSimulatingPhysics(d);
        auto it = g_seen.find(d);
        const bool fresh = it == g_seen.end();
        bool say = fresh;
        if (!fresh) {
            const float dx = at.X - it->second.at.X, dy = at.Y - it->second.at.Y, dz = at.Z - it->second.at.Z;
            say = std::sqrt(dx * dx + dy * dy + dz * dz) > 5.f || parent != it->second.parent || sim != it->second.sim;
        }
        if (say && (!fresh || t > 15000u + 2500u))
            UE_LOGI("[KDISC] %ls %ls key '%ls' at (%.0f,%.0f,%.0f) parent %ls sim=%d%s t=%llu", R::ClassNameOf(d).c_str(),
                    R::ToString(R::NameOf(d)).c_str(), ue_wrap::prop::GetKeyString(d).c_str(), at.X, at.Y, at.Z,
                    parent ? R::ClassNameOf(parent).c_str() : L"-", sim ? 1 : 0, fresh ? " (new)" : "",
                    static_cast<unsigned long long>(t));
        if (say || fresh) g_seen[d] = Seen{at, parent, sim, true};
        else it->second.live = true;
    }
    for (auto it = g_seen.begin(); it != g_seen.end();) {
        if (!it->second.live) {
            UE_LOGI("[KDISC] a disc at (%.0f,%.0f,%.0f) is gone t=%llu", it->second.at.X, it->second.at.Y, it->second.at.Z,
                    static_cast<unsigned long long>(t));
            it = g_seen.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace coop::dev::kdisc_watch
