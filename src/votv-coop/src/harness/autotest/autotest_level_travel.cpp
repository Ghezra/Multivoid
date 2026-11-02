// harness/autotest/autotest_level_travel.cpp -- TEST-ONLY: one peer takes a level travel in a live session.
//
// VOTVCOOP_TRAVEL_WHO (host or client) calls lib_C::loadLevel(VOTVCOOP_TRAVEL_LEVEL) as the game's own sites do, with its
// pawn as the author, and each peer logs its world and its pawn for VOTVCOOP_TRAVEL_WATCH seconds. A measurement: it
// judges nothing, the session's own lines say what became of it. Gated by VOTVCOOP_RUN_LEVEL_TRAVEL; the folder is
// VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"

#include "coop/comms/chat_feed.h"
#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/screen_state.h"
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace harness::autotest {
namespace {

namespace E  = ue_wrap::engine;
namespace R  = ue_wrap::reflection;
namespace WI = ue_wrap::world_identity;

std::string g_dir;

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Mark(const char* name) {
    std::string tmp = g_dir + "\\" + name + ".tmp";
    { std::ofstream(tmp, std::ios::binary | std::ios::trunc) << "x"; }
    ::MoveFileExA(tmp.c_str(), (g_dir + "\\" + name).c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool Await(const char* name, int seconds) {
    for (int i = 0; i < seconds * 10; ++i) {
        if (std::ifstream(g_dir + "\\" + name)) return true;
        ::Sleep(100);
    }
    UE_LOGW("level_travel: VERDICT ABORT -- no '%hs' from the other peer", name);
    return false;
}

void WaitInWorld() {
    void* pawn = nullptr;
    bool ready = !IsClientRole();
    for (int i = 0; i < 480 && (!pawn || !ready); ++i) {
        OnGameThread([&] {
            pawn = coop::players::Registry::Get().Local();
            if (IsClientRole()) ready = coop::net_pump::HasAnnouncedWorldReady();
        });
        if (!pawn || !ready) ::Sleep(500);
    }
}

void Travel(const std::string& level) {
    OnGameThread([&] {
        void* pawn = coop::players::Registry::Get().Local();
        void* lib = R::FindClassDefaultObject(L"lib_C");
        void* fn = lib ? R::FindFunction(R::ClassOf(lib), L"loadLevel") : nullptr;
        if (!pawn || !fn) {
            UE_LOGW("level_travel: VERDICT ABORT -- pawn=%p loadLevel=%p", pawn, fn);
            return;
        }
        const std::wstring w(level.begin(), level.end());
        const R::FName name = ue_wrap::fname_utils::StringToFName(w.c_str());
        ue_wrap::ParamFrame f(fn);
        f.SetRaw(L"level", &name, sizeof(name));
        // The game's own sites all pass the same token, and a level outside its list quits the game without it.
        const std::string optionEnv = coop::config::ReadEnv("VOTVCOOP_TRAVEL_OPTION");
        std::wstring option(optionEnv.begin(), optionEnv.end());
        R::FString fs{};
        fs.Data = option.data();
        fs.Num = static_cast<int32_t>(option.size()) + 1;
        fs.Max = fs.Num;
        f.SetRaw(L"option", &fs, sizeof(fs));
        f.Set<bool>(L"clearSubArea", true);
        f.Set<void*>(L"__WorldContext", pawn);
        const bool ok = ue_wrap::Call(lib, f);
        UE_LOGI("level_travel: lib_C::loadLevel(\"%hs\") dispatched=%d", level.c_str(), ok ? 1 : 0);
    });
}

void WriteBool(void* actor, const wchar_t* name, bool on) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!R::FindBoolProperty(R::ClassOf(actor), name, off, mask)) {
        UE_LOGW("level_travel: no bool '%ls' on the site", name);
        return;
    }
    uint8_t& b = static_cast<uint8_t*>(actor)[off];
    b = on ? (b | mask) : (b & ~mask);
}

void WriteLevel(void* actor, const std::string& level) {
    const int32_t off = R::FindPropertyOffset(R::ClassOf(actor), L"level");
    const std::wstring w(level.begin(), level.end());
    const R::FName name = ue_wrap::fname_utils::StringToFName(w.c_str());
    if (off >= 0) std::memcpy(static_cast<uint8_t*>(actor) + off, &name, sizeof(name));
}

// One way out of the main world: its class, its own function that reaches the travel ("" for a puddle, which walks to the
// player and sinks it on its own), and where it stands.
struct Site {
    const char* name;
    const wchar_t* path;
    const wchar_t* fn;
    bool beside;  // at the player, not above
};
constexpr Site kSites[] = {
    {"well", L"/Game/objects/wellTp.wellTp_C", L"wokeUp", false},
    {"shake", L"/Game/objects/prop_shake.prop_shake_C", L"BndEvt__prop_shake_Audio_K2Node_ComponentBoundEvent_0_OnAudioFinished__DelegateSignature", false},
    {"skull", L"/Game/objects/skullzone.skullzone_C", L"BndEvt__dest_K2Node_ComponentBoundEvent_2_ComponentBeginOverlapSignature__DelegateSignature", false},
    {"puddle", L"/Game/objects/misc/puddle.puddle_C", L"", true},
    {"biotoilet", L"/Game/objects/prop_biotoilet.prop_biotoilet_C", L"actionOptionIndex", false},
    {"insomniac", L"/Game/objects/insomniac.insomniac_C", L"OnCompleted_1A9050824C2776F602A04D9627382DA6", false},
    {"openlevel", L"/Game/objects/triggers/trigger_openLevel.trigger_openLevel_C", L"runTrigger", false},
    {"maptransition", L"/Game/objects/triggers/trigger_box_openMapTransition.trigger_box_openMapTransition_C",
     L"BndEvt__Box_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature", false},
};

// Spawns the site's class above the player and runs its own path to the travel, for what it leaves behind. A placed trigger
// is given VOTVCOOP_TRAVEL_LEVEL.
void FireSite(const std::string& site, const std::string& level) {
    OnGameThread([&] {
        void* pawn = coop::players::Registry::Get().Local();
        ue_wrap::FVector at{};
        if (!pawn || !E::TryGetActorLocation(pawn, at)) return;
        const Site* s = nullptr;
        for (const Site& k : kSites)
            if (site == k.name) s = &k;
        void* cls = s ? ue_wrap::asset_load::LoadObjectByPath(s->path) : nullptr;
        const ue_wrap::FVector where = s && s->beside ? ue_wrap::FVector{at.X + 150.f, at.Y, at.Z} : ue_wrap::FVector{at.X, at.Y, at.Z + 3000.f};
        // A plain insomniac's begin play ends it far from the player or early in the save; its event flag skips that.
        void* actor = !cls ? nullptr
                    : site == "insomniac" ? E::SpawnActor(cls, where, false, [](void*, void* a) { WriteBool(a, L"NewVar_0", true); })
                                          : E::SpawnActor(cls, where);
        if (actor && !*s->fn) {
            UE_LOGI("level_travel: site %hs spawned beside the player", site.c_str());
            return;
        }
        void* fn = actor ? R::FindFunction(cls, s->fn) : nullptr;
        if (!fn) {
            UE_LOGW("level_travel: VERDICT ABORT -- site %hs: class=%p actor=%p fn=%p", site.c_str(), cls, actor, fn);
            return;
        }
        ue_wrap::ParamFrame f(fn);
        if (site == "skull" || site == "maptransition") f.Set<void*>(L"OtherActor", pawn);
        if (site == "biotoilet") {
            WriteBool(actor, L"NewVar_0_0", true);
            f.Set<void*>(L"player", pawn);
            f.Set<uint8_t>(L"action", 4);
        }
        if (site == "insomniac") {
            WriteBool(actor, L"spooker", false);
            WriteBool(actor, L"NewVar_0", true);
        }
        if (site == "openlevel") f.Set<void*>(L"owner", pawn);
        if (site == "openlevel" || site == "maptransition") WriteLevel(actor, level);
        const bool ok = ue_wrap::Call(actor, f);
        UE_LOGI("level_travel: site %hs fired ok=%d", site.c_str(), ok ? 1 : 0);
    });
}

void Report() {
    OnGameThread([] {
        const ue_wrap::engine::ScreenState s = ue_wrap::engine::ReadScreenState();
        void* pawn = coop::players::Registry::Get().Local();
        bool ragdoll = false, dead = false;
        if (pawn) E::ReadMainPlayerRagdollState(pawn, ragdoll, dead);
        void* gi = ue_wrap::world_singleton::GameInstance();
        const int32_t off = gi ? R::FindPropertyOffset(R::ClassOf(gi), L"slotName") : -1;
        const std::wstring slot = off >= 0 ? ue_wrap::field_io::ReadFStringAt(gi, off) : L"";
        UE_LOGI("level_travel: state black=%d/%d paused=%d ragdoll=%d dead=%d slot='%ls'", s.blackOnScreen, s.blackLive,
                E::IsGamePaused() ? 1 : 0, ragdoll ? 1 : 0, dead ? 1 : 0, slot.c_str());
    });
}

void Watch(int seconds) {
    for (int t = 0; t < seconds; t += 2) {
        OnGameThread([&] {
            void* pawn = coop::players::Registry::Get().Local();
            ue_wrap::FVector at{};
            if (pawn) E::TryGetActorLocation(pawn, at);
            UE_LOGI("level_travel: t=%d world=%p kind=%d gen=%u pawn=%p at=(%.0f,%.0f,%.0f) settled=%d", t,
                    WI::CurrentWorld(), static_cast<int>(WI::CurrentWorldKind()), WI::Generation(), pawn, at.X, at.Y, at.Z,
                    coop::net_pump::IsWorldSettled() ? 1 : 0);
        });
        Report();
        ::Sleep(2000);
    }
}

}  // namespace

DWORD WINAPI LevelTravelThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    const std::string level = coop::config::ReadEnv("VOTVCOOP_TRAVEL_LEVEL");
    if (g_dir.empty() || level.empty()) {
        UE_LOGW("level_travel: VERDICT ABORT -- VOTVCOOP_CAN_DIR or VOTVCOOP_TRAVEL_LEVEL is unset");
        return 0;
    }
    const std::string watchEnv = coop::config::ReadEnv("VOTVCOOP_TRAVEL_WATCH");
    const int watch = watchEnv.empty() ? 90 : std::atoi(watchEnv.c_str());
    const bool iTravel = (coop::config::ReadEnv("VOTVCOOP_TRAVEL_WHO") == "client") == IsClientRole();
    WaitInWorld();
    if (IsClientRole()) {
        // The client is the later: both are in once it says so.
        ::Sleep(5000);
        Mark("client_ready");
    } else if (!Await("client_ready", 300)) {
        return 0;
    }
    ::Sleep(5000);
    Watch(6);
    const std::string site = coop::config::ReadEnv("VOTVCOOP_TRAVEL_SITE");
    if (iTravel) {
        if (site.empty()) Travel(level);
        else FireSite(site, level);
    }
    Watch(watch > 8 ? 8 : watch);
    OnGameThread([] {
        coop::chat_feed::ForEachRow([](const coop::chat_feed::RowView& r) {
            UE_LOGI("level_travel: chat row '%.*hs'", static_cast<int>(r.line.size()), r.line.data());
        });
    });
    if (watch > 8) Watch(watch - 8);
    UE_LOGI("level_travel: DONE");
    return 0;
}

}  // namespace harness::autotest
