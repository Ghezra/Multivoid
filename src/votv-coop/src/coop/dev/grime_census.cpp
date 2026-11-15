// coop/dev/grime_census.cpp -- [dev] the world's grime decals counted (VOTVCOOP_GRIME_CENSUS=1), for Ghezra/Multivoid
// issue 36: 20 s after the world is up and every 30 s after, per grime class (the walls' cracks, the dust, the spills),
// how many decals stand and their summed `process`, the dirt the game paints them by (`[GRIME]`). Needs no session.

#include "coop/dev/grime_census.h"

#include "coop/config/config.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/devices/grime.h"

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

namespace coop::dev::grime_census {
namespace {

namespace R  = ue_wrap::reflection;
namespace OI = ue_wrap::object_index;

uint64_t g_startAt = 0, g_nextAt = 0;

struct Tally {
    int n = 0;
    double process = 0.0;
};

}  // namespace

void Tick() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_GRIME_CENSUS") == "1";
    if (!on || !ue_wrap::grime::EnsureResolved()) return;
    const uint64_t now = ::GetTickCount64();
    if (!g_startAt) {
        g_startAt = now;
        g_nextAt = now + 20000u;
    }
    if (now < g_nextAt) return;
    g_nextAt = now + 30000u;
    void* const base = OI::ClassByName(L"grime_C");
    if (!base) return;
    // Every live class descended from grime_C (the cracks, the dust, the spills), then each one's decals.
    std::map<std::wstring, Tally> byClass;
    struct Ctx {
        std::map<std::wstring, Tally>* out;
        void* base;
    } ctx{&byClass, base};
    OI::ForEachClass([](void* c, void* cls, void*) {
        auto* x = static_cast<Ctx*>(c);
        if (!R::IsDescendantOfAny(cls, &x->base, 1)) return;
        OI::ForEachInstance(cls, [](void* c2, void* obj, int32_t) {
            auto* y = static_cast<Ctx*>(c2);
            if (!obj || !R::IsLive(obj) || R::NameStartsWith(R::NameOf(obj), L"Default__")) return;
            float p = 0.f;
            Tally& t = (*y->out)[R::ClassNameOf(obj)];
            ++t.n;
            if (ue_wrap::grime::ReadProcess(obj, p)) t.process += p;
        }, c);
    }, &ctx);
    int n = 0;
    double total = 0.0;
    for (const auto& [cls, t] : byClass) {
        n += t.n;
        total += t.process;
        UE_LOGI("[GRIME] %ls n=%d process=%.1f", cls.c_str(), t.n, t.process);
    }
    UE_LOGI("[GRIME] all n=%d process=%.1f at %llu s", n, total, static_cast<unsigned long long>((now - g_startAt) / 1000));
}

}  // namespace coop::dev::grime_census
