// coop/dev/issue42_probe.cpp -- [dev] Ghezra/Multivoid issue 42's 5-hour logs, staged on the rig
// (VOTVCOOP_ISSUE42_PROBE=<mode> on every peer). Times run from the probe's arm; the host ends with `[I42] DONE`.
//   roach    60 s the host adds 3 roaches by itself and moves 2 of them 300 m off; every peer counts its own.
//   bigbox   60 s the host puts 80 drive records of ~800 B into a world container; every peer reports each container
//            of 50 records or more (count and hash); the host changes a box whose record is 50 KB or more and
//            publishes it, and every peer reports each such box.
//   reseed   60 s slot 1 spawns a beebox, a shrimp, a jar and a cardboard box of its own and grabs each in turn; the
//            host's re-scan must not send them back with eid 0.
//   loops    the timed client loops, with VOTVCOOP_DEV_REFUSE on the clients: 60 s the host turns the alarm on (off at
//            90 s), plays a radio it spawned at 55 s (Custom Content on everywhere), opens the first coordinate
//            tower's panel and spawns a kerfur; every peer reports the alarm, the radio, the towers and its pieces.

#include "coop/dev/issue42_probe.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/dev/spawn_npc.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/props/container_contents_sync.h"
#include "coop/props/container_record_edit.h"
#include "coop/props/prop_record_wire.h"
#include "coop/props/prop_save_data.h"
#include "ue_wrap/actors/build_piece.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/devices/coord_tower.h"
#include "ue_wrap/world/media_device.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/actors/save_record.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
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

// The first cardboard box whose record is 50 KB or more, changed live: its last string grows by a word, through its
// own loadData, and the record is published, as a box a player fills is.
void EditBigBox(coop::net::Session* s) {
    void* const cls = ue_wrap::object_index::ClassByName(L"prop_box_C");
    void* box = nullptr;
    ue_wrap::save_record::SaveRecord r;
    if (cls)
        ue_wrap::object_index::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            void*& pick = *static_cast<void**>(ctx);
            ue_wrap::save_record::SaveRecord rec;
            if (!pick && obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__") &&
                ue_wrap::save_record::CaptureRecord(obj, rec) &&
                coop::props::prop_record_wire::RecordBytes(rec) >= 50000)
                pick = obj;
        }, &box);
    if (!box || !ue_wrap::save_record::CaptureRecord(box, r) || r.strings.empty() || r.strings.back().empty()) {
        UE_LOGW("[I42] host: no cardboard box with a record of 50 KB or more to change (%p)", box);
        return;
    }
    r.strings.back().back() += L" mv42";
    const bool applied = ue_wrap::save_record::ApplyRecord(box, r, ue_wrap::save_record::ClassLoad::LookupOnly);
    size_t bytes = 0;
    const std::wstring key = ue_wrap::prop::GetInteractableKeyString(box);
    const bool pub = coop::prop_save_data::Publish(s, box, key, &bytes);
    UE_LOGI("[I42] host: box key=%ls changed live (applied %d), its record published %d (%zu B)", key.c_str(),
            applied ? 1 : 0, pub ? 1 : 0, bytes);
}

void TickBigBox(coop::net::Session* s, uint64_t t, bool host, int& step) {
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
    EditBigBox(s);
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

// ---- loops ----
std::vector<void*> AllOf(const wchar_t* cls) {
    std::vector<void*> out;
    if (void* const c = ue_wrap::object_index::ClassByName(cls))
        ue_wrap::object_index::ForEachInstance(c, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

bool AlarmActive(void* trig, bool& out) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!trig || !R::FindBoolProperty(R::ClassOf(trig), L"active", off, mask)) return false;
    out = (static_cast<const uint8_t*>(trig)[off] & mask) != 0;
    return true;
}

bool RunAlarm(bool on) {
    const std::vector<void*> t = AllOf(L"trigger_alarm_C");
    void* const fn = t.empty() ? nullptr : R::FindDispatchFunctionCached(R::ClassOf(t[0]), L"runTrigger");
    ue_wrap::ParamFrame f(fn);
    void* owner = nullptr;
    return fn && f.valid() && f.Set(L"owner", owner) && f.Set(L"index", static_cast<int32_t>(on ? 1 : 0)) &&
           ue_wrap::Call(t[0], f);
}

// The game's Custom Content rule on in this process and never saved: a radio opens no link without it. Every peer's.
void AllowCustomContent() {
    void* const instance = R::FindObjectByClass(L"mainGameInstance_C");
    void* const cls = instance ? R::ClassOf(instance) : nullptr;
    const int32_t rules = cls ? R::FindPropertyOffset(cls, L"gameRules") : -1;
    void* const type = cls ? R::PropertyInnerStruct(cls, L"gameRules") : nullptr;
    const int32_t rule = type ? R::FindPropertyOffsetByPrefix(type, L"customContent_") : -1;
    if (rules >= 0 && rule >= 0) static_cast<uint8_t*>(instance)[rules + rule] = 1;
    UE_LOGI("[I42] the Custom Content rule set on here (ok=%d)", rules >= 0 && rule >= 0 ? 1 : 0);
}

void* g_radio = nullptr;
uint64_t g_radioUntil = 0;

void TickLoops(uint64_t t, bool host, void* me, int& step) {
    namespace MD = ue_wrap::media_device;
    namespace CT = ue_wrap::coord_tower;
    if (!host || !me) return;
    if (step == 0 && t >= 55000) {
        step = 1;
        g_radio = Spawn(L"prop_radio_internet_C", Ahead(me, 200.f, 100.f));
        UE_LOGI("[I42] host: a radio %p", g_radio);
    }
    if (step == 1 && t >= 60000) {
        step = 2;
        UE_LOGI("[I42] host: the alarm on (%d)", RunAlarm(true) ? 1 : 0);
        const std::vector<void*> towers = AllOf(L"coordRadarDish_C");
        const bool used = !towers.empty() && CT::Use(towers[0], me, CT::LookAt{CT::Part::Retract, -1});
        UE_LOGI("[I42] host: the first of %zu coordinate towers' panel used (%d)", towers.size(), used ? 1 : 0);
        FVector at = Ahead(me, 300.f, -100.f);
        void* const kerfur = coop::dev::spawn_npc::SpawnAt(L"kerfurOmega_C", {at.X, at.Y, at.Z + 60.f});
        UE_LOGI("[I42] host: a kerfur %p", kerfur);
        g_radioUntil = Now() + 30000;
    }
    if (step == 2 && g_radio && R::IsLive(g_radio)) {
        MD::State st;
        st.track = MD::CatalogTrack(g_radio, 0);
        st.playing = true;
        const MD::Apply r = MD::ApplyState(g_radio, st);
        if (r == MD::Apply::Done || Now() > g_radioUntil) {
            step = 3;
            UE_LOGI("[I42] host: the radio plays track %llx (%s)", static_cast<unsigned long long>(st.track),
                    r == MD::Apply::Done ? "done" : (r == MD::Apply::NoSuchTrack ? "no such track" : "still waiting"));
        }
    }
    if (step >= 2 && step < 9 && t >= 90000) {
        step = 9;
        UE_LOGI("[I42] host: the alarm off (%d)", RunAlarm(false) ? 1 : 0);
    }
}

void ReportLoops(const char* role) {
    namespace MD = ue_wrap::media_device;
    namespace CT = ue_wrap::coord_tower;
    bool alarm = false;
    const std::vector<void*> trig = AllOf(L"trigger_alarm_C");
    const bool ar = !trig.empty() && AlarmActive(trig[0], alarm);
    std::string towers;
    for (void* tw : AllOf(L"coordRadarDish_C")) {
        CT::State st;
        towers += CT::Read(tw, st) ? (st.opened ? " open" : " shut") : " ?";
    }
    std::string radio = "none";
    for (void* r : AllOf(L"prop_radio_internet_C")) {
        MD::State st;
        if (MD::Read(r, st) && st.track) {
            char b[64];
            std::snprintf(b, sizeof(b), "track %llx playing %d", static_cast<unsigned long long>(st.track), st.playing);
            radio = b;
        }
    }
    std::vector<void*> pieces;
    ue_wrap::build_piece::List(pieces);
    int walls = 0, nails = 0;
    for (void* pc : pieces) {
        const auto k = ue_wrap::build_piece::KindOf(pc);
        walls += k == ue_wrap::build_piece::Kind::Wall;
        nails += k == ue_wrap::build_piece::Kind::Nail;
    }
    UE_LOGI("[I42] %s alarm %s; towers%s; radio %s; %zu pieces (%d walls, %d nails)", role,
            ar ? (alarm ? "on" : "off") : "?", towers.c_str(), radio.c_str(), pieces.size(), walls, nails);
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
        if (Mode() == "loops") AllowCustomContent();
    }
    const uint64_t t = Now() - g_armedAt;
    void* const me = coop::players::Registry::Get().Local();
    const std::string& m = Mode();
    if (m == "roach") TickRoach(t, host, me, g_step);
    else if (m == "bigbox") TickBigBox(session, t, host, g_step);
    else if (m == "reseed") TickReseed(t, host, me, g_step);
    else if (m == "loops") TickLoops(t, host, me, g_step);
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
    if (m == "loops") ReportLoops(role);
    if (m == "bigbox") {
        CC::ForEachWorldContainer(&ReportOne, role);
        ReportBigBoxes(role);
    }
}

}  // namespace coop::dev::issue42_probe
