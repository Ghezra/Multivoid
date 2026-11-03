// harness/autotest/autotest_build.cpp -- TEST-ONLY: nails and custom walls are the host's, and every peer has them.
//
// A. The host fires a nail gun's projectile down through two cutting boards: the client has the nail, tied to its copies
//    of the same two boards. B. The client fires one into a board of the host's: the host makes it under the client's key
//    and the client keeps its own. C. The client takes the host's nail away (the nail's own addDamage): it goes on the host,
//    and the one nail item it drops is on both. D. The host takes the client's nail away, the same. E. The host places an
//    unfinished wall: the client has it and counts no bricks. F. The host's wall is finished (concrete, the game's own
//    buildWall): the client has the finished wall with the host's cracks. G. A wet concrete wall, made as buildWall makes
//    one: the client's copy does not dry; the host's dries, and the client has the dry one. H. A hit on the client's copy breaks nothing on either. I. The host breaks it: it goes here and there. J. The client
//    places an unfinished wall: the host makes it and the client keeps its own. K. The client is killed and rejoins: every
//    piece of the host's is here once (VOTVCOOP_BUILD_REJOIN=1 runs that half). Gated by VOTVCOOP_RUN_BUILD; the folder is
//    VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "coop/player/puppet_drive.h"
#include "coop/player/remote_player.h"
#include "coop/props/prop_key_index.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/actors/build_piece.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace harness::autotest {
namespace {

namespace B = ue_wrap::build_piece;
namespace E = ue_wrap::engine;
namespace F = ue_wrap::prop;
namespace R = ue_wrap::reflection;

constexpr const wchar_t* kBoardPath = L"/Game/objects/prop_workbench_cuttingboard.prop_workbench_cuttingboard_C";
constexpr const wchar_t* kProjectilePath = L"/Game/objects/nailProjectile.nailProjectile_C";
constexpr const wchar_t* kNailPath = L"/Game/objects/nail.nail_C";
constexpr const wchar_t* kUnfinishedPath = L"/Game/objects/customWall_unfinished.customWall_unfinished_C";
constexpr const wchar_t* kWetPath = L"/Game/objects/customWall_wetConcrete.customWall_wetConcrete_C";

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

bool Await(const char* name, std::string* body = nullptr, int seconds = 240) {
    for (int i = 0; i < seconds * 10; ++i) {
        std::ifstream f(g_dir + "\\" + name, std::ios::binary);
        if (f) {
            if (body) body->assign(std::istreambuf_iterator<char>(f), {});
            return true;
        }
        ::Sleep(100);
    }
    UE_LOGW("build-drill: VERDICT ABORT -- no '%hs' from the other peer", name);
    return false;
}

void Check(bool ok, const char* what) {
    UE_LOGI("build-drill: %hs %hs", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fails;
}

bool WaitFor(const std::function<bool()>& ok, int seconds) {
    for (int i = 0; i < seconds * 10; ++i) {
        bool v = false;
        OnGameThread([&] { v = ok(); });
        if (v) return true;
        ::Sleep(100);
    }
    return false;
}

std::string Narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }
std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string one;
    while (std::getline(ss, one, '|')) out.push_back(one);
    return out;
}

void* Me() { return coop::players::Registry::Get().Local(); }

template <class T>
bool ReadField(void* obj, const wchar_t* name, T& out) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return false;
    std::memcpy(&out, static_cast<const uint8_t*>(obj) + off, sizeof(out));
    return true;
}

template <class T>
void WriteField(void* obj, const wchar_t* name, const T& v) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off >= 0) std::memcpy(static_cast<uint8_t*>(obj) + off, &v, sizeof(v));
}

bool ReadFlag(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    return obj && R::FindBoolProperty(R::ClassOf(obj), name, off, mask) && off >= 0 &&
           (static_cast<const uint8_t*>(obj)[off] & mask) != 0;
}

bool CallFn(void* obj, const wchar_t* name, const std::function<void(ue_wrap::ParamFrame&)>& set = {}) {
    void* fn = obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), name) : nullptr;
    if (!fn) return false;
    ue_wrap::ParamFrame f(fn);
    if (!f.valid()) return false;
    if (set) set(f);
    return ue_wrap::Call(obj, f);
}

void* PieceByKey(const std::string& key) {
    std::vector<void*> all;
    B::List(all);
    for (void* p : all)
        if (Narrow(B::KeyOf(p)) == key) return p;
    return nullptr;
}

size_t PiecesWithKey(const std::string& key) {
    std::vector<void*> all;
    B::List(all);
    size_t n = 0;
    for (void* p : all) n += Narrow(B::KeyOf(p)) == key;
    return n;
}

std::set<std::string> AllKeys() {
    std::vector<void*> all;
    B::List(all);
    std::set<std::string> out;
    for (void* p : all) out.insert(Narrow(B::KeyOf(p)));
    return out;
}

std::string ClassName(void* obj) { return Narrow(R::ToString(R::NameOf(R::ClassOf(obj)))); }

std::string PropKey(void* actor) { return actor ? Narrow(F::GetInteractableKeyString(actor)) : std::string("None"); }

size_t CountClass(const wchar_t* name) {
    void* cls = ue_wrap::object_index::ClassByName(name);
    size_t n = 0;
    if (cls)
        ue_wrap::object_index::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (R::IsLive(obj)) ++*static_cast<size_t*>(ctx);
        }, &n);
    return n;
}

size_t NailItems() { return CountClass(L"prop_nail_C"); }

// A point `ahead` cm before `actor` along its forward, `up` cm above it.
bool Before(void* actor, float ahead, float up, ue_wrap::FVector& out) {
    ue_wrap::FVector at{};
    if (!actor || !E::TryGetActorLocation(actor, at)) return false;
    const ue_wrap::FVector fwd = E::GetActorForwardVector(actor);
    out = {at.X + fwd.X * ahead, at.Y + fwd.Y * ahead, at.Z + up};
    return true;
}

void* SpawnBoard(const ue_wrap::FVector& at) {
    void* cls = ue_wrap::asset_load::LoadObjectByPath(kBoardPath);
    return cls ? E::SpawnActor(cls, at) : nullptr;
}

// A nail gun's shot as prop_nailgun's own fires it: the projectile, its force and its nail's class, straight down from
// 120 cm above `target`.
bool FireDownAt(void* target) {
    void* proj = ue_wrap::asset_load::LoadObjectByPath(kProjectilePath);
    void* nail = ue_wrap::asset_load::LoadObjectByPath(kNailPath);
    ue_wrap::FVector at{};
    if (!proj || !nail || !target || !E::TryGetActorLocation(target, at)) return false;
    const ue_wrap::FVector from{at.X, at.Y, at.Z + 120.f};
    void* p = E::BeginDeferredSpawn(proj, from, {});
    if (!p) return false;
    WriteField(p, L"Force", ue_wrap::FVector{0.f, 0.f, -10000.f});
    WriteField(p, L"nailType", nail);
    return E::FinishDeferredSpawn(p, from, {});
}

// The nail whose first end is `target`, or null.
void* NailInto(void* target) {
    std::vector<void*> all;
    B::List(all);
    for (void* p : all)
        if (B::KindOf(p) == B::Kind::Nail && (B::NailEndA(p) == target || B::NailEndB(p) == target)) return p;
    return nullptr;
}

std::string NailRow(void* nail) {
    return Narrow(B::KeyOf(nail)) + "|" + PropKey(B::NailEndA(nail)) + "|" + PropKey(B::NailEndB(nail)) + "|" +
           (ReadFlag(nail, L"nailed") ? "1" : "0");
}

bool TakeNail(void* nail) {
    return CallFn(nail, L"addDamage", [](ue_wrap::ParamFrame& f) { f.Set(L"damage", 1.f); });
}

// An unfinished wall as the wall builder's addWall places it: shape and material set before it finishes spawning.
void* PlaceUnfinished(void* actor) {
    void* cls = ue_wrap::asset_load::LoadObjectByPath(kUnfinishedPath);
    ue_wrap::FVector at{};
    if (!cls || !Before(actor, 300.f, 20.f, at)) return nullptr;
    void* w = E::BeginDeferredSpawn(cls, at, {});
    if (!w) return nullptr;
    WriteField(w, L"Shape", int32_t{0});
    WriteField(w, L"Material", int32_t{0});
    return E::FinishDeferredSpawn(w, at, {}) ? w : nullptr;
}

std::string WallRow(void* w) {
    float h = 0.f, m = 0.f;
    ReadField(w, L"health", h);
    ReadField(w, L"maxHealth", m);
    return Narrow(B::KeyOf(w)) + "|" + ClassName(w) + "|" + std::to_string(m > 0.f ? h / m : -1.f);
}

// The finished or wet wall that is not among `before`.
void* NewWall(const std::set<std::string>& before) {
    std::vector<void*> all;
    B::List(all);
    for (void* p : all) {
        const B::Kind k = B::KindOf(p);
        if ((k == B::Kind::Wall || k == B::Kind::WetWall) && !before.count(Narrow(B::KeyOf(p)))) return p;
    }
    return nullptr;
}

bool WorldReady() {
    bool ready = false;
    for (int i = 0; i < 480 && !ready; ++i) {
        OnGameThread([&] { ready = Me() && coop::net_pump::HasAnnouncedWorldReady(); });
        if (!ready) ::Sleep(500);
    }
    return ready;
}

// ---- HOST ----------------------------------------------------------------------------------------------------------

void RunHost() {
    if (!Await("client_ready", nullptr, 400)) return;
    // A.
    void* boardB = nullptr;
    OnGameThread([&] {
        ue_wrap::FVector at{};
        if (!Before(Me(), 200.f, 0.f, at)) return;
        SpawnBoard(at);
        boardB = SpawnBoard({at.X, at.Y, at.Z + 15.f});
    });
    ::Sleep(4000);
    OnGameThread([&] { FireDownAt(boardB); });
    void* nailA = nullptr;
    Check(WaitFor([&] { return (nailA = NailInto(boardB)) != nullptr; }, 5), "the host's nail went into its board");
    std::string rowA;
    OnGameThread([&] { if (nailA) rowA = NailRow(nailA); });
    UE_LOGI("build-drill: host nail %hs", rowA.c_str());
    Mark("a_nail", rowA);
    if (!Await("a_ok")) return;
    // B.
    std::string boardC;
    OnGameThread([&] {
        void* puppet = coop::puppet_drive::Puppet(1).GetActor();
        ue_wrap::FVector at{};
        if (puppet && Before(puppet, 150.f, 0.f, at)) boardC = PropKey(SpawnBoard(at));
    });
    Mark("b_board", boardC);
    size_t items0 = 0;
    OnGameThread([&] { items0 = NailItems(); });
    std::string b;
    if (!Await("b_nail", &b)) return;
    const auto bn = Split(b);
    Check(bn.size() == 4 && WaitFor([&] {
              void* n = PieceByKey(bn[0]);
              return n && PropKey(B::NailEndA(n)) == bn[1];
          }, 10),
          "the client's nail is made here, in the same board");
    Mark("b_ok");
    // C.
    const auto an = Split(rowA);
    if (!Await("c_took") || an.empty()) return;
    Check(WaitFor([&] { return !PieceByKey(an[0]); }, 8), "the client took the host's nail away here");
    ::Sleep(3000);
    size_t items1 = 0;
    OnGameThread([&] { items1 = NailItems(); });
    Mark("c_host", std::to_string(items1 - items0));
    UE_LOGI("build-drill: the host's nail item count went %zu -> %zu", items0, items1);
    if (!Await("c_ok")) return;
    // D.
    bool took = false;
    OnGameThread([&] {
        if (bn.size() == 4) took = TakeNail(PieceByKey(bn[0]));
    });
    Check(took && WaitFor([&] { return !PieceByKey(bn[0]); }, 3), "the host took the client's nail away");
    ::Sleep(3000);
    size_t items2 = 0;
    OnGameThread([&] { items2 = NailItems(); });
    Mark("d_host", std::to_string(items2 - items1));
    if (!Await("d_ok")) return;
    // E.
    std::string unf;
    OnGameThread([&] {
        void* w = PlaceUnfinished(Me());
        int32_t req = -1;
        if (w && ReadField(w, L"requirement", req)) unf = Narrow(B::KeyOf(w)) + "|" + std::to_string(req);
    });
    UE_LOGI("build-drill: host unfinished wall %hs", unf.c_str());
    Mark("e_unf", unf);
    if (!Await("e_ok")) return;
    // F.
    std::set<std::string> before;
    void* wall = nullptr;
    OnGameThread([&] {
        before = AllKeys();
        void* w = PieceByKey(Split(unf)[0]);
        const int32_t off = w ? R::FindPropertyOffset(R::ClassOf(w), L"wallType") : -1;
        if (off >= 0) ue_wrap::field_io::WriteFStringField(w, off, L"concrete");
        CallFn(w, L"buildWall");
    });
    Check(WaitFor([&] { return (wall = NewWall(before)) != nullptr; }, 5), "the host's wall was finished");
    std::string fw;
    OnGameThread([&] { if (wall) fw = WallRow(wall); });
    UE_LOGI("build-drill: host wall %hs", fw.c_str());
    Mark("f_wall", fw);
    if (!Await("f_ok")) return;
    // G. Concrete finishes dry; a wet wall is made as buildWall makes one, beside it.
    void* wetWall = nullptr;
    OnGameThread([&] {
        void* cls = ue_wrap::asset_load::LoadObjectByPath(kWetPath);
        ue_wrap::FVector at{};
        if (!cls || !Before(Me(), 300.f, 20.f, at)) return;
        at.Z += 250.f;
        wetWall = E::BeginDeferredSpawn(cls, at, {});
        if (!wetWall) return;
        WriteField(wetWall, L"Shape", int32_t{0});
        const int32_t off = R::FindPropertyOffset(R::ClassOf(wetWall), L"wallType");
        if (off >= 0) ue_wrap::field_io::WriteFStringField(wetWall, off, L"concrete");
        if (!E::FinishDeferredSpawn(wetWall, at, {})) wetWall = nullptr;
    });
    std::string ww;
    OnGameThread([&] { if (wetWall) ww = WallRow(wetWall); });
    UE_LOGI("build-drill: host wet wall %hs", ww.c_str());
    Mark("g_wet", ww);
    if (!Await("g_wet_ok")) return;
    void* dry = nullptr;
    OnGameThread([&] {
        before = AllKeys();
        float t = 0.f;
        if (wetWall && ReadField(wetWall, L"timeToDry", t)) WriteField(wetWall, L"dryTime", t);
    });
    Check(WaitFor([&] { return (dry = NewWall(before)) != nullptr; }, 15), "the host's wet wall dried");
    OnGameThread([&] {
        if (wall && R::IsLive(wall)) E::DestroyActor(wall);   // the concrete wall is done with
    });
    std::string gw;
    OnGameThread([&] { if (dry) gw = WallRow(dry); });
    UE_LOGI("build-drill: host dry wall %hs", gw.c_str());
    Mark("g_dry", gw);
    if (!Await("g_ok")) return;
    // H.
    std::string h0, h1;
    OnGameThread([&] { if (dry) h0 = WallRow(dry); });
    if (!Await("h_hit")) return;
    ::Sleep(2000);
    OnGameThread([&] { if (dry && R::IsLive(dry)) h1 = WallRow(dry); });
    Check(!h0.empty() && h0 == h1, "the client's hit on its copy left the host's wall as it was");
    Mark("h_ok");
    // I.
    OnGameThread([&] {
        ue_wrap::FVector at{};
        if (dry && E::TryGetActorLocation(dry, at))
            CallFn(dry, L"Damage", [&](ue_wrap::ParamFrame& f) {
                f.Set(L"damage", 1.0e7f);
                f.Set(L"location", at);
            });
    });
    Check(WaitFor([&] { return !dry || !R::IsLive(dry); }, 3), "the host broke its wall");
    Mark("i_gone", Split(gw)[0]);
    if (!Await("i_ok")) return;
    // J.
    std::string j;
    if (!Await("j_unf", &j)) return;
    Check(WaitFor([&] { return PieceByKey(j) != nullptr; }, 10), "the client's unfinished wall is made here");
    Mark("j_ok");
    // K.
    if (!Await("client_killed", nullptr, 300)) return;
    std::string list;
    OnGameThread([&] {
        for (const std::string& k : AllKeys()) list += k + "|";
    });
    Mark("k_list", list);
    UE_LOGI("build-drill: %zu pieces live for the rejoin", Split(list).size());
    Await("rejoin_done", nullptr, 400);
    UE_LOGI("build-drill: DONE on host fails=%d", g_fails);
}

// ---- CLIENT --------------------------------------------------------------------------------------------------------

void RunClient() {
    WorldReady();
    ::Sleep(8000);
    Mark("client_ready");
    // A.
    std::string a;
    if (!Await("a_nail", &a)) return;
    const auto an = Split(a);
    Check(an.size() == 4 && WaitFor([&] { return PieceByKey(an[0]) != nullptr; }, 10), "the host's nail is made here");
    OnGameThread([&] {
        void* n = an.size() == 4 ? PieceByKey(an[0]) : nullptr;
        const std::string row = n ? NailRow(n) : std::string();
        UE_LOGI("build-drill: client nail %hs (host %hs)", row.c_str(), a.c_str());
        Check(row == a, "it is in this copy's same two boards, nailed as the host's");
        void* drop = nullptr;
        Check(n && ReadField(n, L"nailDrop", drop) && !drop, "this copy drops no nail item of its own");
    });
    Mark("a_ok");
    // B.
    std::string board;
    if (!Await("b_board", &board)) return;
    void* target = nullptr;
    Check(WaitFor([&] { return (target = coop::prop_key_index::Find(Widen(board))) != nullptr; }, 10),
          "the host's board for this player is here");
    ::Sleep(3000);
    OnGameThread([&] { FireDownAt(target); });
    void* mine = nullptr;
    Check(WaitFor([&] { return (mine = NailInto(target)) != nullptr; }, 5), "this player's nail went into the board");
    std::string rowB;
    OnGameThread([&] { if (mine) rowB = NailRow(mine); });
    Mark("b_nail", rowB);
    if (!Await("b_ok")) return;
    const auto bn = Split(rowB);
    ::Sleep(11000);   // past the lane's wait for the host's answer
    Check(bn.size() == 4 && WaitFor([&] { return PiecesWithKey(bn[0]) == 1; }, 2),
          "this player's nail stands here once, the host's answer");
    // C.
    size_t items0 = 0;
    bool took = false;
    OnGameThread([&] {
        items0 = NailItems();
        took = TakeNail(PieceByKey(an[0]));
    });
    Check(took && WaitFor([&] { return !PieceByKey(an[0]); }, 3), "this player took the host's nail away");
    Mark("c_took");
    std::string c;
    if (!Await("c_host", &c)) return;
    ::Sleep(3000);
    size_t items1 = 0;
    OnGameThread([&] { items1 = NailItems(); });
    UE_LOGI("build-drill: nail items here %zu -> %zu, on the host +%hs", items0, items1, c.c_str());
    Check(std::to_string(items1 - items0) == c && c == "1", "the nail it dropped lies here once, as on the host");
    Mark("c_ok");
    // D.
    std::string d;
    if (!Await("d_host", &d)) return;
    ::Sleep(3000);
    size_t items2 = 0;
    OnGameThread([&] { items2 = NailItems(); });
    Check(bn.size() == 4 && !PieceByKey(bn[0]), "the host took this player's nail away here");
    Check(std::to_string(items2 - items1) == d && d == "1", "the nail the host's dropped lies here once");
    Mark("d_ok");
    // E.
    std::string e;
    if (!Await("e_unf", &e)) return;
    const auto en = Split(e);
    Check(en.size() == 2 && WaitFor([&] { return PieceByKey(en[0]) != nullptr; }, 10), "the host's unfinished wall is here");
    OnGameThread([&] {
        void* w = en.size() == 2 ? PieceByKey(en[0]) : nullptr;
        int32_t req = -1;
        Check(w && ReadField(w, L"requirement", req) && std::to_string(req) == en[1], "it needs the host's bricks");
        void* overlap = nullptr;
        Check(w && ReadField(w, L"Overlap", overlap) && overlap && E::GetComponentCollisionEnabled(overlap) == 0,
              "this copy counts no bricks");
    });
    Mark("e_ok");
    // F.
    std::string f;
    if (!Await("f_wall", &f)) return;
    const auto fw = Split(f);
    Check(fw.size() == 3 && WaitFor([&] { return PieceByKey(fw[0]) != nullptr; }, 10), "the host's finished wall is here");
    OnGameThread([&] {
        void* w = fw.size() == 3 ? PieceByKey(fw[0]) : nullptr;
        const std::string row = w ? WallRow(w) : std::string();
        UE_LOGI("build-drill: client wall %hs (host %hs)", row.c_str(), f.c_str());
        Check(row == f, "of the host's class, cracked as the host's");
        Check(!PieceByKey(en[0]), "the unfinished wall went here");
        if (w && B::KindOf(w) == B::Kind::WetWall) Check(!E::IsActorTickEnabled(w), "this wet copy does not dry");
    });
    Mark("f_ok");
    // G.
    std::string wet;
    if (!Await("g_wet", &wet)) return;
    const auto wn = Split(wet);
    Check(wn.size() == 3 && WaitFor([&] { return PieceByKey(wn[0]) != nullptr; }, 10), "the host's wet wall is here");
    OnGameThread([&] {
        void* w = wn.size() == 3 ? PieceByKey(wn[0]) : nullptr;
        Check(w && WallRow(w) == wet, "of the host's class and cracks");
        Check(w && !E::IsActorTickEnabled(w), "this wet copy does not dry");
    });
    Mark("g_wet_ok");
    std::string g;
    if (!Await("g_dry", &g)) return;
    const auto gw = Split(g);
    Check(gw.size() == 3 && WaitFor([&] { return PieceByKey(gw[0]) != nullptr; }, 10), "the host's dry wall is here");
    OnGameThread([&] {
        void* w = gw.size() == 3 ? PieceByKey(gw[0]) : nullptr;
        Check(w && WallRow(w) == g, "of the host's class and cracks");
        Check(wn.size() == 3 && !PieceByKey(wn[0]), "the wet wall went here");
    });
    Mark("g_ok");
    // H.
    OnGameThread([&] {
        void* w = PieceByKey(gw[0]);
        ue_wrap::FVector at{};
        if (w && E::TryGetActorLocation(w, at))
            CallFn(w, L"Damage", [&](ue_wrap::ParamFrame& p) {
                p.Set(L"damage", 500.f);
                p.Set(L"location", at);
            });
    });
    ::Sleep(1000);
    Check(WaitFor([&] { return PieceByKey(gw[0]) != nullptr; }, 1), "a hit on this copy does not break it");
    Mark("h_hit");
    if (!Await("h_ok")) return;
    // I.
    if (!Await("i_gone")) return;
    Check(WaitFor([&] { return !PieceByKey(gw[0]); }, 5), "the wall the host broke went here");
    Mark("i_ok");
    // J.
    std::string j;
    OnGameThread([&] {
        void* w = PlaceUnfinished(Me());
        if (w) j = Narrow(B::KeyOf(w));
    });
    UE_LOGI("build-drill: this player's unfinished wall '%hs'", j.c_str());
    Mark("j_unf", j);
    if (!Await("j_ok")) return;
    ::Sleep(11000);
    Check(WaitFor([&] { return PiecesWithKey(j) == 1; }, 2), "this player's unfinished wall stands here once");
    Mark("client_done");
    UE_LOGI("build-drill: DONE on client fails=%d", g_fails);
}

void RunRejoin() {
    WorldReady();
    std::string k;
    if (!Await("k_list", &k, 120)) return;
    std::vector<std::string> keys;
    for (const std::string& one : Split(k))
        if (!one.empty()) keys.push_back(one);
    ::Sleep(6000);
    Check(WaitFor([&] {
              for (const std::string& one : keys)
                  if (PiecesWithKey(one) != 1) return false;
              return true;
          }, 20),
          "every piece of the host's is here once after the rejoin");
    size_t here = 0;
    OnGameThread([&] { here = AllKeys().size(); });
    UE_LOGI("build-drill: rejoin -- %zu pieces on the host, %zu here", keys.size(), here);
    Check(here == keys.size(), "and none besides");
    Mark("rejoin_done");
    UE_LOGI("build-drill: REJOIN DONE fails=%d", g_fails);
}

}  // namespace

DWORD WINAPI BuildThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("build-drill: VERDICT ABORT -- VOTVCOOP_CAN_DIR is not set");
        return 0;
    }
    if (!IsClientRole()) RunHost();
    else if (coop::config::ReadEnv("VOTVCOOP_BUILD_REJOIN") == "1") RunRejoin();
    else RunClient();
    return 0;
}

}  // namespace harness::autotest
