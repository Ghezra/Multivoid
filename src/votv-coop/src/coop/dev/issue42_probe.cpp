// coop/dev/issue42_probe.cpp -- [dev] Ghezra/Multivoid issue 42's 5-hour logs, staged on the rig
// (VOTVCOOP_ISSUE42_PROBE=<mode> on every peer). Times run from the probe's arm; the host ends with `[I42] DONE`.
//   roach    60 s the host adds 3 roaches by itself and moves 2 of them 300 m off; every peer counts its own.
//   bigbox   60 s the host puts 80 drive records of ~800 B into a world container; every peer reports each container
//            of 50 records or more (count and hash), and each cardboard box whose record is 50 KB or more.
//   reseed   60 s slot 1 spawns a beebox, a shrimp, a jar and a cardboard box of its own and grabs each in turn; the
//            host's re-scan must not send them back with eid 0.

#include "coop/dev/issue42_probe.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/props/container_contents_sync.h"
#include "coop/props/container_record_edit.h"
#include "coop/props/prop_record_wire.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/actors/save_record.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::issue42_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace CR = ue_wrap::class_resolve;
namespace CC = coop::props::container_contents_sync;
namespace RE = coop::props::container_record_edit;
using ue_wrap::FVector;

const std::string& Mode() {
    static const std::string m = coop::config::ReadEnv("VOTVCOOP_ISSUE42_PROBE");
    return m;
}

uint64_t Now() { return ::GetTickCount64(); }

void* Spawn(const wchar_t* cls, const FVector& at) {
    void* const c = CR::ResolveClass(cls, CR::AnyFamily(), CR::Load::Immediate).cls;
    void* const a = c ? E::BeginDeferredSpawn(c, at, {}) : nullptr;
    return a && E::FinishDeferredSpawn(a, at, {}) ? a : nullptr;
}

FVector Ahead(void* me, float cm, float side = 0.f) {
    FVector at{};
    E::TryGetActorLocation(me, at);
    const FVector f = E::GetActorForwardVector(me);
    return {at.X + f.X * cm - f.Y * side, at.Y + f.Y * cm + f.X * side, at.Z};
}

unsigned Eid(void* a) { return static_cast<unsigned>(coop::element::Registry::Get().EidForActor(a)); }

// ---- roach ----
struct FArr { void* data; int32_t num; int32_t max; };

void* Master() {
    void* const gm = ue_wrap::world_singleton::Gamemode();
    const int32_t off = gm ? R::FindPropertyOffset(R::ClassOf(gm), L"cockroachMaster") : -1;
    void* const m = off >= 0 ? *reinterpret_cast<void* const*>(static_cast<uint8_t*>(gm) + off) : nullptr;
    return m && R::IsLive(m) ? m : nullptr;
}

std::vector<void*> Roaches(void* master) {
    std::vector<void*> out;
    const int32_t off = master ? R::FindPropertyOffset(R::ClassOf(master), L"roaches") : -1;
    if (off < 0) return out;
    const FArr* a = reinterpret_cast<const FArr*>(static_cast<uint8_t*>(master) + off);
    for (int32_t i = 0; a->data && i < a->num; ++i) {
        void* const c = static_cast<void* const*>(a->data)[i];
        if (c && R::IsLive(c)) out.push_back(c);
    }
    return out;
}

bool AddRoach(void* master, const FVector& at) {
    void* const fn = R::FindFunction(R::ClassOf(master), L"addRoach");
    ue_wrap::ParamFrame f(fn);
    const float loc[3] = {at.X, at.Y, at.Z}, size[3] = {1.f, 1.f, 1.f};
    return fn && f.valid() && f.SetRaw(L"Location", loc, sizeof(loc)) && f.SetRaw(L"Size", size, sizeof(size)) &&
           f.Set<bool>(L"bypassCheck", true) && ue_wrap::Call(master, f);
}

bool MoveComp(void* comp, const FVector& to) {
    void* const cls = R::FindClass(L"SceneComponent");
    void* const fn = cls ? R::FindFunction(cls, L"K2_SetWorldLocation") : nullptr;
    ue_wrap::ParamFrame f(fn);
    const float loc[3] = {to.X, to.Y, to.Z};
    return fn && f.valid() && f.SetRaw(L"NewLocation", loc, sizeof(loc)) && f.Set<bool>(L"bTeleport", true) &&
           ue_wrap::Call(comp, f);
}

void TickRoach(uint64_t t, bool host, void* me, int& step) {
    if (!host || step != 0 || t < 60000) return;
    step = 1;
    void* const m = Master();
    const size_t before = Roaches(m).size();
    int added = 0;
    for (int i = 0; m && i < 3; ++i) added += AddRoach(m, Ahead(me, 200.f, -100.f + 100.f * i)) ? 1 : 0;
    std::vector<void*> all = Roaches(m);
    int moved = 0;
    for (size_t i = before; i < all.size() && moved < 2; ++i)
        moved += MoveComp(all[i], Ahead(me, 30000.f, 200.f * moved)) ? 1 : 0;
    UE_LOGI("[I42] host: %d roach(es) added near, %d moved 300 m off; %zu now", added, moved, Roaches(m).size());
}

// ---- bigbox ----
// The first world container under 100 records whose slice is writable: the one the records go into.
void PickOne(uint32_t eid, void* ctx) {
    uint32_t& pick = *static_cast<uint32_t*>(ctx);
    uint64_t h = 0;
    uint32_t n = 0;
    if (!pick && CC::ContentHashOf(eid, h, n) && n < 100 && RE::Probe(eid) == RE::EditResult::Done) pick = eid;
}

void TickBigBox(uint64_t t, bool host, int& step) {
    if (!host || step != 0 || t < 60000) return;
    step = 1;
    uint32_t eid = 0;
    CC::ForEachWorldContainer(&PickOne, &eid);
    CR::ResolveClass(L"prop_drive_C", CR::AnyFamily(), CR::Load::Immediate);
    int put = 0;
    RE::EditResult last = RE::EditResult::Done;
    for (int i = 0; eid && i < 80; ++i) {
        ue_wrap::save_record::SaveRecord r;
        r.className = L"prop_drive_C";
        r.key = L"mv42_" + std::to_wstring(i);
        r.xform = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f};
        std::wstring text;
        for (int w = 0; text.size() < 800; ++w)
            text += L"signal_" + std::to_wstring((i * 131 + w * 17) % 977) + L"_obj ";
        r.strings = {{text}};
        last = RE::InsertAt(eid, 0xFFFF, r, RE::Source::Host);
        if (last != RE::EditResult::Done) break;
        ++put;
    }
    if (eid) CC::MarkHostWrite(eid);
    void* const owner = eid ? coop::element::LivePropActor(eid) : nullptr;
    UE_LOGI("[I42] host: container eid=%u (%ls) took %d records (last %s)", eid,
            owner ? R::ClassNameOf(owner).c_str() : L"none", put, RE::EditResultName(last));
}

// Each cardboard box whose own record is near the chunk ceiling or past it: its size and a hash of its values.
void ReportBigBoxes(const char* role) {
    void* const cls = ue_wrap::object_index::ClassByName(L"prop_box_C");
    if (!cls) return;
    ue_wrap::object_index::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
        ue_wrap::save_record::SaveRecord r;
        if (!obj || !R::IsLive(obj) || R::NameStartsWith(R::NameOf(obj), L"Default__") ||
            !ue_wrap::save_record::CaptureRecord(obj, r))
            return;
        const size_t bytes = coop::props::prop_record_wire::RecordBytes(r);
        if (bytes < 50000) return;   // near the chunk ceiling or past it
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const void* p, size_t n) {
            for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t*>(p)[i]) * 1099511628211ull;
        };
        for (const auto& g : r.strings) for (const std::wstring& s : g) mix(s.data(), s.size() * sizeof(wchar_t));
        for (const auto& g : r.ints) mix(g.data(), g.size() * sizeof(int32_t));
        for (const auto& g : r.names) for (const std::wstring& s : g) mix(s.data(), s.size() * sizeof(wchar_t));
        UE_LOGI("[I42] %s box key=%ls record=%zu B values=%016llx", static_cast<const char*>(ctx),
                ue_wrap::prop::GetInteractableKeyString(obj).c_str(), bytes, static_cast<unsigned long long>(h));
    }, const_cast<char*>(role));
}

void ReportOne(uint32_t eid, void* ctx) {
    uint64_t h = 0;
    uint32_t n = 0;
    if (CC::ContentHashOf(eid, h, n) && n >= 50)
        UE_LOGI("[I42] %s container eid=%u records=%u hash=%016llx", static_cast<const char*>(ctx), eid, n,
                static_cast<unsigned long long>(h));
}

// ---- reseed ----
// The client's own props, grabbed one at a time and let go 2 s later: a held untracked prop is what crosses
// (trash_collect's broadcast), as the session's scrap wood and bag pieces did.
std::vector<void*> g_own;
size_t g_next = 0;
std::unique_ptr<coop::director::AimedGrab> g_grab;
uint64_t g_heldAt = 0;

void TickReseed(uint64_t t, bool host, void* me, int& step) {
    if (host || !me || coop::players::Registry::Get().LocalPeerId() != 1) return;
    if (step == 0 && t >= 60000) {
        step = 1;
        const wchar_t* const cls[] = {L"prop_beebox_C", L"prop_cookingFood_shrimp_C", L"prop_dirtyjar_C", L"prop_box_C"};
        for (int i = 0; i < 4; ++i)
            if (void* const a = Spawn(cls[i], Ahead(me, 130.f, -75.f + 50.f * i))) g_own.push_back(a);
        UE_LOGI("[I42] client: spawned %zu props of its own (a beebox, a shrimp, a jar, a box)", g_own.size());
    }
    if (step != 1 || g_next >= g_own.size()) return;
    void* const prop = g_own[g_next];
    if (!g_grab && !g_heldAt) {
        if (!R::IsLive(prop)) { ++g_next; return; }
        g_grab = std::make_unique<coop::director::AimedGrab>(me, prop);
    }
    if (g_grab) {
        const coop::director::GrabState st = g_grab->Tick();
        if (st == coop::director::GrabState::Working) return;
        UE_LOGI("[I42] client: grab of %ls %s%s", R::ClassNameOf(prop).c_str(),
                st == coop::director::GrabState::Grabbed ? "took" : "failed: ",
                st == coop::director::GrabState::Grabbed ? "" : g_grab->Why());
        g_grab.reset();
        if (st != coop::director::GrabState::Grabbed) { ++g_next; return; }
        g_heldAt = Now();
        return;
    }
    if (Now() - g_heldAt < 2000) return;
    E::ReleaseMainPlayerGrabIfHolding(me, prop);
    g_heldAt = 0;
    ++g_next;
}

uint64_t g_armedAt = 0, g_reportAt = 0;
int g_step = 0;
bool g_done = false;

}  // namespace

void Tick(coop::net::Session* session) {
    if (Mode().empty() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_armedAt) {
        g_armedAt = Now();
        UE_LOGI("[I42] probe armed role=%s mode=%s", host ? "host" : "client", Mode().c_str());
    }
    const uint64_t t = Now() - g_armedAt;
    void* const me = coop::players::Registry::Get().Local();
    const std::string& m = Mode();
    if (m == "roach") TickRoach(t, host, me, g_step);
    else if (m == "bigbox") TickBigBox(t, host, g_step);
    else if (m == "reseed") TickReseed(t, host, me, g_step);
    if (host && !g_done && t >= 120000) {
        g_done = true;
        UE_LOGI("[I42] DONE");
    }
    if (t < 55000 || t > 130000 || Now() < g_reportAt) return;
    g_reportAt = Now() + 10000;
    char role[16];
    std::snprintf(role, sizeof(role), "%s%u", host ? "host" : "slot",
                  host ? 0u : static_cast<unsigned>(coop::players::Registry::Get().LocalPeerId()));
    if (m == "roach") {
        FVector at{};
        E::TryGetActorLocation(me, at);
        UE_LOGI("[I42] %s roaches here: %zu (player at %.0f,%.0f)", role, Roaches(Master()).size(), at.X, at.Y);
    }
    if (m == "bigbox") {
        CC::ForEachWorldContainer(&ReportOne, role);
        ReportBigBoxes(role);
    }
}

}  // namespace coop::dev::issue42_probe
