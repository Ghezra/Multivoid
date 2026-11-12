// harness/autotest/autotest_backrooms_probe.cpp -- TEST-ONLY spike: can the backrooms map run as a level instance inside
// the session's world? The host loads VOTVCOOP_BACKROOMS_MAP (default sl_backroomsObsolete) through
// LevelStreamingDynamic::LoadLevelInstance far below the map, waits for it, tallies the actor classes of the loaded level,
// teleports itself to a PlayerStart of that level (else the instance's origin), and logs its place and health each
// second for 40 s, with screenshots. Gated by VOTVCOOP_RUN_BACKROOMS_PROBE.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "harness/screenshot.h"
#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_fingerprint.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

namespace harness::autotest {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void* Me() { return coop::players::Registry::Get().Local(); }

void PlaceMe(const ue_wrap::FVector& at) {
    void* f = R::FindFunction(R::ClassOf(Me()), L"teleportWObackrooms");
    ue_wrap::ParamFrame pf(f);
    ue_wrap::FTransform t{};
    t.TX = at.X; t.TY = at.Y; t.TZ = at.Z;
    if (f && pf.valid() && pf.Set<ue_wrap::FTransform>(L"newTransform", t)) ue_wrap::Call(Me(), pf);
}

bool CallBool(void* obj, const wchar_t* fn) {
    void* f = obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), fn) : nullptr;
    ue_wrap::ParamFrame pf(f);
    return f && pf.valid() && ue_wrap::Call(obj, pf) && pf.Get<bool>(L"ReturnValue");
}

void* CallObj(void* obj, const wchar_t* fn) {
    void* f = obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), fn) : nullptr;
    ue_wrap::ParamFrame pf(f);
    return (f && pf.valid() && ue_wrap::Call(obj, pf)) ? pf.Get<void*>(L"ReturnValue") : nullptr;
}

void Run() {
    if (IsClientRole()) return;
    ::Sleep(25000);
    std::string map = coop::config::ReadEnv("VOTVCOOP_BACKROOMS_MAP");
    if (map.empty()) map = "sl_backroomsObsolete";
    const std::wstring wmap(map.begin(), map.end());
    const ue_wrap::FVector origin{0.f, 0.f, -50000.f};
    void* streaming = nullptr;
    OnGameThread([&] {
        void* cdo = R::FindClassDefaultObject(L"LevelStreamingDynamic");
        void* fn = cdo ? R::FindFunction(R::ClassOf(cdo), L"LoadLevelInstance") : nullptr;
        ue_wrap::ParamFrame pf(fn);
        std::wstring name = wmap;
        R::FString s{const_cast<wchar_t*>(name.c_str()), static_cast<int32_t>(name.size() + 1),
                     static_cast<int32_t>(name.size() + 1)};
        const bool set = fn && pf.valid() && pf.Set<void*>(L"WorldContextObject", Me()) &&
                         pf.Set<R::FString>(L"LevelName", s) && pf.Set<ue_wrap::FVector>(L"Location", origin) &&
                         pf.Set<ue_wrap::FRotator>(L"Rotation", ue_wrap::FRotator{});
        const bool called = set && ue_wrap::Call(cdo, pf);
        streaming = called ? pf.Get<void*>(L"ReturnValue") : nullptr;
        UE_LOGI("backrooms-probe: LoadLevelInstance('%ls') fn=%p set=%d called=%d success=%d streaming=%p", wmap.c_str(),
                fn, set ? 1 : 0, called ? 1 : 0, called && pf.Get<bool>(L"bOutSuccess") ? 1 : 0, streaming);
    });
    if (!streaming) { UE_LOGW("backrooms-probe: ABORT -- no streaming level"); return; }
    void* level = nullptr;
    for (int t = 0; t < 120 && !level; ++t) {
        ::Sleep(500);
        OnGameThread([&] { if (CallBool(streaming, L"IsLevelLoaded")) level = CallObj(streaming, L"GetLoadedLevel"); });
    }
    UE_LOGI("backrooms-probe: level %p loaded=%d visible=%d", level, level ? 1 : 0,
            level ? (CallBool(streaming, L"IsLevelVisible") ? 1 : 0) : 0);
    if (!level) return;
    ::Sleep(2000);
    ue_wrap::FVector start = origin;
    OnGameThread([&] {
        struct Ctx { void* level; std::map<std::wstring, int> tally; void* playerStart; std::map<std::wstring, void*> cls; } c{level, {}, nullptr, {}};
        OI::ForEachClass([](void* ctx, void* cls, void*) {
            auto* k = static_cast<Ctx*>(ctx);
            OI::ForEachInstance(cls, [](void* ctx2, void* o, int32_t) {
                auto* k2 = static_cast<Ctx*>(ctx2);
                if (!R::IsLive(o) || R::OuterOf(o) != k2->level) return;
                const std::wstring n = R::ClassNameOf(o);
                ++k2->tally[n];
                k2->cls[n] = R::ClassOf(o);
                if (!k2->playerStart && n == L"PlayerStart") k2->playerStart = o;
            }, k);
        }, &c);
        std::wstring line;
        int total = 0;
        for (auto& [n, count] : c.tally) {
            total += count;
            wchar_t buf[96];
            std::swprintf(buf, 96, L" %ls=%d", n.c_str(), count);
            line += buf;
            if (line.size() > 1500) { UE_LOGI("backrooms-probe: classes%ls", line.c_str()); line.clear(); }
        }
        if (!line.empty()) UE_LOGI("backrooms-probe: classes%ls", line.c_str());
        // The level's own Blueprint classes (the _C ones) as script text, for reading its exits.
        const std::string dir = coop::config::ReadEnv("VOTVCOOP_BACKROOMS_DUMP_DIR");
        if (!dir.empty())
            for (auto& [n, cls] : c.cls) {
                if (n.size() < 3 || n.substr(n.size() - 2) != L"_C") continue;
                std::ofstream out(dir + "/" + std::string(n.begin(), n.end()) + "_script.txt", std::ios::binary | std::ios::trunc);
                for (const R::ObjectRef& o : R::ChildObjectsOf(cls)) {
                    if (o.className != L"Function") continue;
                    out << "=== " << std::string(o.name.begin(), o.name.end()) << '\n'
                        << ue_wrap::script_fingerprint::Of(o.object).text << '\n';
                }
            }
        if (c.playerStart) E::TryGetActorLocation(c.playerStart, start);
        UE_LOGI("backrooms-probe: %d objects in the level; PlayerStart %p at (%.0f,%.0f,%.0f)", total, c.playerStart,
                start.X, start.Y, start.Z);
        {
            // Did the level's own placed player take the gamemode's reference?
            void* gm = ue_wrap::world_singleton::Gamemode();
            const int32_t off = gm ? R::FindPropertyOffset(R::ClassOf(gm), L"mainPlayer") : -1;
            void* mp = off >= 0 ? *reinterpret_cast<void**>(static_cast<char*>(gm) + off) : nullptr;
            UE_LOGI("backrooms-probe: gamemode.mainPlayer=%p, this player=%p (%hs)", mp, Me(),
                    mp == Me() ? "the same" : "DIFFERENT");
        }
        PlaceMe({start.X, start.Y, start.Z + 100.f});
    });
    for (int t = 0; t < 40; ++t) {
        ::Sleep(1000);
        OnGameThread([t] {
            ue_wrap::FVector at{};
            if (Me()) E::TryGetActorLocation(Me(), at);
            float hp = 0.f;
            ue_wrap::vitals::Read(ue_wrap::vitals::Field::Health, &hp);
            UE_LOGI("backrooms-probe: STATE t=%d at=(%.0f,%.0f,%.0f) health=%.0f", t, at.X, at.Y, at.Z, hp);
        });
        if (t == 3 || t == 20) {
            wchar_t label[32];
            std::swprintf(label, 32, L"backrooms-t%d", t);
            harness::screenshot::Capture(label);
        }
    }
    UE_LOGI("backrooms-probe: DONE");
}

}  // namespace

DWORD WINAPI BackroomsProbeThread(LPVOID /*arg*/) {
    Run();
    return 0;
}

}  // namespace harness::autotest
