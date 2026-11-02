// harness/autotest/autotest_ambient_field.cpp -- TEST-ONLY: one set of hexahive holes, sus holes, beehive branches and walking
// mannequins across two peers.
//
// A hexahive stands before the client joins, so its field reaches the joiner. Then the host runs each spawner's own body and the
// client checks what arrives, that its own rolls do not stand, and that a walker follows the host's. Gated by
// VOTVCOOP_RUN_AMBIENT_FIELD; the folder is VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/dev/mannequin_drill.h"
#include "coop/player/players_registry.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/actors/ambient_creature.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/core/walk_census.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace harness::autotest {
namespace {

namespace AC = ue_wrap::ambient_creature;
namespace E = ue_wrap::engine;
namespace R = ue_wrap::reflection;
namespace OI = ue_wrap::object_index;

std::string g_dir;
int g_fails = 0;

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Mark(const char* name, const std::string& body = "x") {
    std::string tmp = g_dir + "\\" + name + ".tmp";
    { std::ofstream(tmp, std::ios::binary | std::ios::trunc) << body; }
    ::MoveFileExA(tmp.c_str(), (g_dir + "\\" + name).c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool Await(const char* name, std::string* body = nullptr, int seconds = 300) {
    for (int i = 0; i < seconds * 10; ++i) {
        std::ifstream f(g_dir + "\\" + name, std::ios::binary);
        std::string s;
        if (f && std::getline(f, s) && !s.empty()) {
            if (body) *body = s;
            return true;
        }
        ::Sleep(100);
    }
    UE_LOGW("ambient-field: VERDICT ABORT -- no '%hs' from the other peer", name);
    return false;
}

void Check(bool ok, const char* what) {
    UE_LOGI("ambient-field: %hs %hs", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fails;
}

void* FirstOf(const wchar_t* cls) {
    void* found = nullptr;
    if (void* c = OI::ClassByName(cls))
        OI::ForEachInstance(c, [](void* ctx, void* obj, int32_t) {
            if (R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__")) *static_cast<void**>(ctx) = obj;
        }, &found);
    return found;
}

size_t CountOf(const wchar_t* cls) {
    size_t n = 0;
    if (void* c = OI::ClassByName(cls))
        OI::ForEachInstance(c, [](void* ctx, void* obj, int32_t) {
            if (R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__")) ++*static_cast<size_t*>(ctx);
        }, &n);
    return n;
}

bool RunTick(const wchar_t* cls) {
    void* ticker = FirstOf(cls);
    void* fn = ticker ? R::FindDispatchFunctionCached(R::ClassOf(ticker), L"ReceiveTick") : nullptr;
    if (!fn) return false;
    ue_wrap::ParamFrame f(fn);
    if (!f.valid()) return false;
    f.Set<float>(L"DeltaSeconds", 0.016f);
    return ue_wrap::Call(ticker, f);
}

bool RunBeehiveSpawn() {
    void* ticker = FirstOf(L"ticker_beehiveSpawner_C");
    void* fn = ticker ? R::FindDispatchFunctionCached(R::ClassOf(ticker), L"Spawn") : nullptr;
    if (!fn) return false;
    ue_wrap::ParamFrame f(fn);
    return f.valid() && ue_wrap::Call(ticker, f);
}

// A sum of every place: the two peers' lists hold the same places when the counts and these agree.
struct Field {
    size_t n = 0;
    double x = 0, y = 0, z = 0;
};

Field ReadField(bool branches) {
    Field f;
    std::vector<void*> v;
    if (branches) AC::ListBranches(v);
    else AC::ListHoles(v);
    for (void* a : v) {
        ue_wrap::FVector at{};
        if (!E::TryGetActorLocation(a, at)) continue;
        ++f.n;
        f.x += at.X;
        f.y += at.Y;
        f.z += at.Z;
    }
    return f;
}

std::string Text(const Field& f) {
    char b[128];
    std::snprintf(b, sizeof(b), "%zu %.1f %.1f %.1f", f.n, f.x, f.y, f.z);
    return b;
}

// Waits for this peer's field to be the host's.
void ExpectField(const std::string& hostText, bool branches, const char* what) {
    Field host;
    if (std::sscanf(hostText.c_str(), "%zu %lf %lf %lf", &host.n, &host.x, &host.y, &host.z) != 4) {
        Check(false, what);
        return;
    }
    Field here;
    bool same = false;
    for (int i = 0; i < 100 && !same; ++i) {
        OnGameThread([&] { here = ReadField(branches); });
        const double tol = 2.0 * static_cast<double>((std::max<size_t>)(host.n, 1));
        same = here.n == host.n && std::fabs(here.x - host.x) < tol && std::fabs(here.y - host.y) < tol && std::fabs(here.z - host.z) < tol;
        if (!same) ::Sleep(100);
    }
    UE_LOGI("ambient-field: %hs here=%hs host=%hs", what, Text(here).c_str(), hostText.c_str());
    Check(same, what);
}

void* LocalPawn() { return coop::players::Registry::Get().Local(); }

void RunHost() {
    void* pawn = nullptr;
    for (int i = 0; i < 480 && !pawn; ++i) {
        OnGameThread([&] { pawn = LocalPawn(); });
        if (!pawn) ::Sleep(500);
    }
    if (!pawn) return;
    ::Sleep(4000);

    // A real hive's own begin play, beside the base: its field and its self-destroy.
    OnGameThread([&] {
        ue_wrap::FVector me{};
        void* cls = OI::ClassByName(L"hexahiveSPawner_C");
        if (!cls) cls = R::FindClass(L"hexahiveSPawner_C");
        if (cls && E::TryGetActorLocation(pawn, me)) E::SpawnActor(cls, {me.X + 1500.f, me.Y, me.Z + 200.f});
    });
    ::Sleep(1000);
    Field hive;
    OnGameThread([&] { hive = ReadField(false); });
    UE_LOGI("ambient-field: the hive made %zu holes", hive.n);
    Check(hive.n > 0, "the hive's own body made a field of holes");
    size_t hiveSpawners = 99;
    OnGameThread([&] { hiveSpawners = CountOf(L"hexahiveSPawner_C"); });
    Check(hiveSpawners == 0, "the hive's spawner destroyed itself");
    Mark("hive", Text(hive));
    if (!Await("hive_done", nullptr, 480)) return;

    // The sus hole's tick rolls by the camera and a trace, so it runs until one stands.
    Field before = hive, after = hive;
    for (int i = 0; i < 400 && after.n == before.n; ++i) {
        OnGameThread([&] { RunTick(L"ticker_susHoleSpawner_C"); after = ReadField(false); });
        ::Sleep(10);
    }
    Check(after.n > before.n, "the sus hole spawner's own body made a hole");
    ::Sleep(500);
    OnGameThread([&] { after = ReadField(false); });
    Mark("sus", Text(after));
    if (!Await("sus_done")) return;

    // One hole ends.
    OnGameThread([&] {
        std::vector<void*> v;
        AC::ListHoles(v);
        if (!v.empty()) AC::Destroy(v.back());
    });
    ::Sleep(500);
    OnGameThread([&] { after = ReadField(false); });
    Mark("hole_end", Text(after));
    if (!Await("hole_end_done")) return;

    // The beehive: a random tree inside the map's box, and the body retries by a Delay when the tree is outside it.
    Field branchesBefore, branches;
    OnGameThread([&] { branchesBefore = ReadField(true); branches = branchesBefore; });
    size_t nestsBefore = 0;
    OnGameThread([&] { nestsBefore = CountOf(L"prop_waspnest_bee_C"); });
    for (int i = 0; i < 90 && branches.n == branchesBefore.n; ++i) {
        OnGameThread([&] { RunBeehiveSpawn(); branches = ReadField(true); });
        if (branches.n == branchesBefore.n) ::Sleep(1000);
    }
    Check(branches.n > branchesBefore.n, "the beehive spawner's own body made a branch");
    // A body that found its tree outside the box waits 5 s and runs again, so more may follow the first.
    ::Sleep(7000);
    OnGameThread([&] { branches = ReadField(true); });
    size_t nests = 0;
    OnGameThread([&] { nests = CountOf(L"prop_waspnest_bee_C"); });
    Check(nests > nestsBefore, "the host's branch has its nest");
    Mark("branch", Text(branches));
    Mark("nests", std::to_string(nests));
    if (!Await("branch_done")) return;

    OnGameThread([&] {
        std::vector<void*> v;
        AC::ListBranches(v);
        if (!v.empty()) AC::Destroy(v.back());
    });
    ::Sleep(1000);
    OnGameThread([&] { branches = ReadField(true); nests = CountOf(L"prop_waspnest_bee_C"); });
    Mark("branch_end", Text(branches));
    Mark("nests_end", std::to_string(nests));
    if (!Await("branch_end_done")) return;

    // The walker: the host looks away, so its brain walks it.
    coop::dev::mannequin_drill::SpawnWalker();
    void* walker = nullptr;
    for (int i = 0; i < 100 && !walker; ++i) {
        ::Sleep(100);
        OnGameThread([&] { std::vector<void*> v; AC::ListWalkers(v); if (!v.empty()) walker = v.back(); });
    }
    Check(walker != nullptr, "a walker stands");
    if (!walker) return;
    Mark("walker");
    if (!Await("walker_seen")) return;
    ue_wrap::FVector start{}, at{};
    OnGameThread([&] { E::TryGetActorLocation(walker, start); });
    float moved = 0.f;
    for (int t = 0; t < 900 && moved < 60.f; ++t) {
        if (t % 5 == 0) {
            char b[96];
            std::snprintf(b, sizeof(b), "%.1f %.1f %.1f", at.X, at.Y, at.Z);
            if (at.X != 0.f) Mark("walker_at", b);
        }
        OnGameThread([&] {
            if (!R::IsLive(walker) || !E::TryGetActorLocation(walker, at)) return;
            ue_wrap::FVector me = E::GetCameraLocation();
            const float yawTo = std::atan2(at.Y - me.Y, at.X - me.X) * 57.29578f;
            E::SetControlRotation(E::GetController(LocalPawn()), ue_wrap::FRotator{0.f, yawTo + 180.f, 0.f});
            const float dx = at.X - start.X, dy = at.Y - start.Y;
            moved = std::sqrt(dx * dx + dy * dy);
        });
        ::Sleep(100);
    }
    UE_LOGI("ambient-field: the host's walker moved %.0f cm", moved);
    Check(moved >= 60.f, "the host's walker walks while the host looks away");
    for (int i = 0; i < 12; ++i) {
        ue_wrap::FVector now{};
        OnGameThread([&] {
            ue_wrap::FVector me = E::GetCameraLocation();
            if (!E::TryGetActorLocation(walker, now)) return;
            const float yawTo = std::atan2(now.Y - me.Y, now.X - me.X) * 57.29578f;
            E::SetControlRotation(E::GetController(LocalPawn()), ue_wrap::FRotator{0.f, yawTo + 180.f, 0.f});
        });
        char b[96];
        std::snprintf(b, sizeof(b), "%.1f %.1f %.1f", now.X, now.Y, now.Z);
        Mark("walker_at", b);
        UE_LOGI("ambient-field: host walker at (%.0f,%.0f,%.0f)", now.X, now.Y, now.Z);
        ::Sleep(1000);
    }
    Mark("walker_end");
    Await("client_done");
    UE_LOGI("ambient-field: DONE on host fails=%d", g_fails);
}

void RunClient() {
    bool ready = false;
    for (int i = 0; i < 480 && !ready; ++i) {
        OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
        if (!ready) ::Sleep(500);
    }
    ::Sleep(8000);
    OnGameThread([&] {
        for (const wchar_t* cls : {L"ticker_hexahiveSpawner_C", L"ticker_susHoleSpawner_C", L"ticker_mannequinSpawner_C"}) {
            void* t = FirstOf(cls);
            UE_LOGI("ambient-field: spawner '%ls' here=%d", cls, t ? 1 : 0);
            Check(t && !E::IsActorTickEnabled(t), "this client's spawner is parked");
        }
    });

    bool timerPaused = false;
    OnGameThread([&] { timerPaused = AC::BeehiveTimerPaused(); });
    Check(timerPaused, "this client's beehive timer is paused");

    std::string body;
    if (!Await("hive", &body)) return;
    ExpectField(body, false, "a joiner has the hive's field, every hole at the host's place");

    // This client's own rolls do not stand.
    Field had, now;
    OnGameThread([&] { had = ReadField(false); });
    for (int i = 0; i < 100; ++i) {
        OnGameThread([] { RunTick(L"ticker_susHoleSpawner_C"); });
        ::Sleep(10);
    }
    ::Sleep(1500);
    OnGameThread([&] { now = ReadField(false); });
    Check(now.n == had.n, "holes this client rolled itself were swept");
    Mark("hive_done");

    if (!Await("sus", &body)) return;
    ExpectField(body, false, "the host's sus hole stands here at its place");
    Mark("sus_done");
    if (!Await("hole_end", &body)) return;
    ExpectField(body, false, "a hole that ended on the host ended here");
    Mark("hole_end_done");

    if (!Await("branch", &body)) return;
    ExpectField(body, true, "the host's branch stands here at its place");
    std::string nests;
    if (!Await("nests", &nests)) return;
    ::Sleep(2000);
    size_t nestsHere = 0;
    OnGameThread([&] { nestsHere = CountOf(L"prop_waspnest_bee_C"); });
    UE_LOGI("ambient-field: nests here=%zu host=%hs", nestsHere, nests.c_str());
    Check(std::to_string(nestsHere) == nests, "the nests are one set, the host's");
    // A roll of this client's own.
    Field branchesHad, branchesNow;
    OnGameThread([&] { branchesHad = ReadField(true); RunBeehiveSpawn(); });
    ::Sleep(1500);
    OnGameThread([&] { branchesNow = ReadField(true); });
    Check(branchesNow.n == branchesHad.n, "a branch this client rolled itself was swept");
    Mark("branch_done");

    if (!Await("branch_end", &body)) return;
    ExpectField(body, true, "a branch that ended on the host ended here");
    if (!Await("nests_end", &nests)) return;
    ::Sleep(2000);
    OnGameThread([&] { nestsHere = CountOf(L"prop_waspnest_bee_C"); });
    UE_LOGI("ambient-field: nests after here=%zu host=%hs", nestsHere, nests.c_str());
    Check(std::to_string(nestsHere) == nests, "its nest ended with it");
    Mark("branch_end_done");

    if (!Await("walker")) return;
    void* walker = nullptr;
    for (int i = 0; i < 300 && !walker; ++i) {
        ::Sleep(100);
        OnGameThread([&] { std::vector<void*> v; AC::ListWalkers(v); if (!v.empty()) walker = v.back(); });
    }
    Check(walker != nullptr, "the host's walker crossed");
    if (!walker) return;
    ::Sleep(1000);
    OnGameThread([&] { Check(!E::IsActorTickEnabled(walker), "the walker's brain is parked here"); });
    ue_wrap::FVector first{}, last{};
    OnGameThread([&] { E::TryGetActorLocation(walker, first); });
    Mark("walker_seen");
    if (!Await("walker_at", nullptr, 120)) return;
    float worst = 0.f;
    int samples = 0;
    int moving = 0;
    for (int i = 0; i < 120; ++i) {
        if (std::ifstream(g_dir + "\\walker_end")) break;
        std::string line;
        { std::ifstream f(g_dir + "\\walker_at", std::ios::binary); std::getline(f, line); }
        float x = 0, y = 0, z = 0;
        if (std::sscanf(line.c_str(), "%f %f %f", &x, &y, &z) == 3) {
            OnGameThread([&] {
                ue_wrap::FVector at{};
                if (!E::TryGetActorLocation(walker, at)) return;
                const float dx = at.X - x, dy = at.Y - y;
                const float d = std::sqrt(dx * dx + dy * dy);
                worst = (std::max)(worst, d);
                last = at;
                ++samples;
                bool standing = true;
                if (AC::ReadWalkerStanding(walker, standing) && !standing) ++moving;
                UE_LOGI("ambient-field: client walker at (%.0f,%.0f,%.0f), %.0f cm from the host's last, standing=%d", at.X, at.Y, at.Z, d, standing ? 1 : 0);
            });
        }
        ::Sleep(1000);
    }
    UE_LOGI("ambient-field: walker worst gap %.0f cm over %d samples, walking in %d", worst, samples, moving);
    const float cdx = last.X - first.X, cdy = last.Y - first.Y;
    const float walked = std::sqrt(cdx * cdx + cdy * cdy);
    UE_LOGI("ambient-field: the client's walker went %.0f cm", walked);
    Check(walked > 40.f, "the client's walker walked with the host's");
    // The host's place is up to a second old, and a far walker runs about 250 cm in that second.
    Check(samples > 0 && worst < 400.f, "the client's walker stays with the host's");
    Check(moving > 0, "the client's walker shows walking while it moves");
    // The lanes' own cost at rest, with a walker standing: whole-array walks of the object table, against the census.
    const unsigned long long walksBefore = ue_wrap::walk_census::ArrayWalkCountTotal();
    ::Sleep(20000);
    const unsigned long long walks = ue_wrap::walk_census::ArrayWalkCountTotal() - walksBefore;
    UE_LOGI("ambient-field: whole-array walks in 20 s: %llu (%.1f a second)", walks, static_cast<double>(walks) / 20.0);
    Mark("client_done");
    UE_LOGI("ambient-field: DONE on client fails=%d", g_fails);
}

}  // namespace

DWORD WINAPI AmbientFieldThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("ambient-field: VERDICT ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    if (IsClientRole()) RunClient();
    else RunHost();
    return 0;
}

}  // namespace harness::autotest
