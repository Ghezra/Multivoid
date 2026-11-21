// coop/dev/sit_probe.cpp -- [dev] a client sits on a chair and the host looks at its body. See the header.
//
// THE CLIENT, from 34 s: the nearest sitBox_C not on an ATV; it stands 70 cm off the box on each side in turn and
// takes the game's own sit (mainPlayer_C::sit, as the box's use calls it) until sittingOn reads set; at 64 s the
// game's own unsit. Every 2 s from 30 s to 76 s: its actor, the box, the half height and sittingOn (`[SIT]`).
// THE HOST, from 40 s: it stands 2 m off the chair, lit, and looks at the body until 50 s; screenshots at 48 s (seated) and 70 s
// (stood). Every 2 s: the body's location and its AnimBP seat pose.
// VOTVCOOP_SIT_PROBE_WHO=host: the host sits instead, by the same steps, and every client logs, every 2 s from 30 s to
// 76 s, the seat it sits on (the box's actor, its class, place and yaw) and how many props within 3 m moved more than
// 5 cm since 30 s, the farthest of them by name (`[SIT] watch`); the host logs the same of its own seat (`watch (host)`).
// VOTVCOOP_SIT_PROBE_SEAT=<piece of a class name> (any case) takes only a box on such a seat, e.g. sofa.

#include "coop/dev/sit_probe.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/dev/director/director.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "harness/screenshot.h"
#include "ue_wrap/actors/puppet.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <cwctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::sit_probe {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_SIT_PROBE") == "1";
    return v;
}

float Dist(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string Narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

void* ObjField(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off < 0 ? nullptr : *reinterpret_cast<void**>(static_cast<uint8_t*>(obj) + off);
}

// VOTVCOOP_SIT_PROBE_SEAT: a piece of the seat's class name (any case) the box must sit on, e.g. sofa.
bool SeatMatches(void* parent) {
    static const std::wstring want = [] {
        std::string v = coop::config::ReadEnv("VOTVCOOP_SIT_PROBE_SEAT");
        for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return std::wstring(v.begin(), v.end());
    }();
    if (want.empty()) return true;
    std::wstring n = R::ClassNameOf(parent);
    for (wchar_t& c : n) c = static_cast<wchar_t>(std::towlower(c));
    return n.find(want) != std::wstring::npos;
}

// The nearest sit box on something other than an ATV.
void* NearestBox(const FVector& from) {
    void* best = nullptr;
    float bestD = 1e30f;
    for (void* b : R::FindObjectsByClass(L"sitBox_C")) {
        if (!b || !R::IsLive(b) || R::NameStartsWith(R::NameOf(b), L"Default__")) continue;
        void* parent = nullptr;
        if (!E::GetAttachParentActor(b, parent) || !parent || ue_wrap::atv::IsAtv(parent) || !SeatMatches(parent)) continue;
        FVector at{};
        if (!E::TryGetActorLocation(b, at)) continue;
        const float d = Dist(at, from);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

bool Sit(void* me, void* box) {
    void* parent = nullptr;
    E::GetAttachParentActor(box, parent);
    void* f = R::FindFunction(R::ClassOf(me), L"sit");
    ue_wrap::ParamFrame pf(f);
    return f && pf.valid() && pf.Set<void*>(L"actor", parent) && pf.Set<void*>(L"component", ObjField(box, L"sitting")) &&
           pf.Set<void*>(L"place", box) && ue_wrap::Call(me, pf);
}

bool Unsit(void* me) {
    void* f = R::FindFunction(R::ClassOf(me), L"unsit");
    ue_wrap::ParamFrame pf(f);
    return f && pf.valid() && ue_wrap::Call(me, pf);
}

// The host's flashlight on, its battery full: the game shows and hides the light and keeps its own intensity.
void LightUp(void* me) {
    if (void* gm = ue_wrap::world_singleton::Gamemode())
        if (void* slot = ObjField(gm, L"saveSlot")) {
            const int32_t off = R::FindPropertyOffset(R::ClassOf(slot), L"battery");
            if (off >= 0) *reinterpret_cast<float*>(static_cast<uint8_t*>(slot) + off) = 100.f;
        }
    E::WriteMainPlayerFlashlight(me, true);
    void* l = E::GetMainPlayerLightR(me);
    const bool lit = l && E::SetSceneComponentVisibility(l, true, false);
    UE_LOGI("[SIT] host: flashlight %s", lit ? "on" : "OFF");
}

void* ClientBody() {
    auto& reg = coop::players::Registry::Get();
    for (int s = 1; s < coop::players::kMaxPeers; ++s)
        if (coop::RemotePlayer* p = reg.Puppet(static_cast<uint8_t>(s)); p && p->valid() && p->actor()) return p->actor();
    return nullptr;
}

uint64_t g_startAt = 0, g_nextLog = 0;
void* g_box = nullptr;
int g_try = 0;
bool g_seated = false, g_unsat = false, g_placed = false, g_shot1 = false, g_shot2 = false;

bool HostSits() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_SIT_PROBE_WHO") == "host";
    return v;
}

// A client watching the host sit: the seat nearest the host's body once it sits, and the props around it.
struct Seen { void* obj; FVector at; };
std::vector<Seen> g_around;
void* g_seat = nullptr;
FVector g_seatAt0{};

void PropsAround(const FVector& c, std::vector<Seen>& out) {
    struct Ctx { FVector c; std::vector<Seen>* out; } ctx{c, &out};
    if (void* base = ue_wrap::object_index::ClassByName(L"prop_C"))
        ue_wrap::object_index::ForEachInstanceOfKind(base, [](void* p, void* obj, int32_t) {
            auto* x = static_cast<Ctx*>(p);
            if (!obj || !R::IsLive(obj) || R::NameStartsWith(R::NameOf(obj), L"Default__")) return;
            FVector a{};
            if (E::TryGetActorLocation(obj, a) && Dist(a, x->c) < 300.f) x->out->push_back({obj, a});
        }, &ctx);
}

void WatchTick(uint64_t t, bool self) {
    coop::RemotePlayer* const hp = self ? nullptr : coop::players::Registry::Get().Puppet(0);
    void* const body = self ? coop::players::Registry::Get().Local() : (hp && hp->valid() ? hp->actor() : nullptr);
    static uint64_t s_next = 0;
    if (!body || t < 30000u || t >= 76000u || Now() < s_next) return;
    s_next = Now() + 2000;
    FVector at{};
    E::TryGetActorLocation(body, at);
    if (!g_seat && t >= 30000u) {
        if (void* box = NearestBox(at)) E::GetAttachParentActor(box, g_seat);
        if (g_seat) { E::TryGetActorLocation(g_seat, g_seatAt0); PropsAround(g_seatAt0, g_around); }
    }
    FVector s{};
    if (g_seat) E::TryGetActorLocation(g_seat, s);
    ue_wrap::FRotator rot{};
    if (g_seat) E::TryGetActorRotation(g_seat, rot);
    const float yaw = rot.Yaw;
    int moved = 0;
    float most = 0.f;
    std::string farName = "-";
    for (const Seen& p : g_around) {
        FVector a{};
        if (!R::IsLive(p.obj) || !E::TryGetActorLocation(p.obj, a)) continue;
        const float d = Dist(a, p.at);
        if (d > 5.f) ++moved;
        if (d > most) { most = d; farName = Narrow(R::ClassNameOf(p.obj)); }
    }
    UE_LOGI("[SIT] watch%s t=%llu: host body (%.0f,%.0f,%.0f); seat %s at (%.1f,%.1f,%.1f) yaw %.1f, %.1f cm from where it "
            "stood; %d of %zu props within 3 m moved > 5 cm, the most %s %.1f cm",
            self ? " (host)" : "", t / 1000ull, at.X, at.Y, at.Z, g_seat ? Narrow(R::ClassNameOf(g_seat)).c_str() : "-", s.X, s.Y, s.Z, yaw,
            g_seat ? Dist(s, g_seatAt0) : -1.f, moved, g_around.size(), farName.c_str(), most);
}

void ClientTick(uint64_t t) {
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    FVector at{};
    E::TryGetActorLocation(me, at);
    void* on = nullptr;
    E::ReadMainPlayerSittingOn(me, on);
    if (!g_seated && !g_box && t >= 34000u) {
        g_box = NearestBox(at);
        FVector b{};
        void* parent = nullptr;
        if (g_box) { E::TryGetActorLocation(g_box, b); E::GetAttachParentActor(g_box, parent); }
        UE_LOGI("[SIT] client: box %p on %s at (%.0f,%.0f,%.0f), %.0f cm away", g_box,
                parent ? Narrow(R::ClassNameOf(parent)).c_str() : "-", b.X, b.Y, b.Z, g_box ? Dist(b, at) : -1.f);
    }
    // One side per 2 s until the sit takes.
    if (g_box && !g_seated && t >= 34000u + 2000u * g_try && g_try < 4) {
        if (on) { g_seated = true; UE_LOGI("[SIT] client: SEATED on %s", Narrow(R::ClassNameOf(on)).c_str()); }
        else {
            FVector b{};
            E::TryGetActorLocation(g_box, b);
            const FVector fwd = E::GetActorForwardVector(g_box);
            const FVector right{-fwd.Y, fwd.X, 0.f};
            const FVector dirs[4] = {fwd, {-fwd.X, -fwd.Y, 0.f}, right, {-right.X, -right.Y, 0.f}};
            const FVector d = dirs[g_try];
            E::SetActorLocation(me, FVector{b.X + d.X * 70.f, b.Y + d.Y * 70.f, b.Z + 70.f});
            const bool ran = Sit(me, g_box);
            UE_LOGI("[SIT] client: try %d from side %d, sit ran=%d", g_try, g_try, ran ? 1 : 0);
            ++g_try;
        }
    }
    if (!g_seated && on) { g_seated = true; UE_LOGI("[SIT] client: SEATED on %s", Narrow(R::ClassNameOf(on)).c_str()); }
    if (g_seated && !g_unsat && t >= 64000u && coop::config::ReadEnv("VOTVCOOP_SIT_PROBE_STAY") != "1") {
        g_unsat = true;
        UE_LOGI("[SIT] client: unsit ran=%d", Unsit(me) ? 1 : 0);
    }
    if (t >= 30000u && t < 76000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 2000;
        FVector b{};
        if (g_box) E::TryGetActorLocation(g_box, b);
        UE_LOGI("[SIT] client t=%llu: actor (%.0f,%.0f,%.0f) box (%.0f,%.0f,%.0f) halfH %.1f sittingOn %s",
                t / 1000ull, at.X, at.Y, at.Z, b.X, b.Y, b.Z, E::GetActorCharacterHalfHeight(me),
                on ? Narrow(R::ClassNameOf(on)).c_str() : "-");
    }
}

void HostTick(uint64_t t) {
    void* const me = coop::players::Registry::Get().Local();
    void* const body = ClientBody();
    if (!me || !body) return;
    FVector at{};
    E::TryGetActorLocation(body, at);
    // From 40 s, one side of the chair the body sits on per 2 s, until a stand there stays on the floor.
    static int s_side = 0;
    static uint64_t s_nextSide = 40000u;
    static FVector s_standAt{};
    FVector mine{};
    E::TryGetActorLocation(me, mine);
    if (g_placed && (std::fabs(mine.Z - s_standAt.Z) > 200.f) && s_side < 4) g_placed = false;  // fell: next side
    if (!g_placed && s_side < 4 && t >= s_nextSide) {
        void* box = NearestBox(at);
        FVector b{};
        if (box && E::TryGetActorLocation(box, b)) {
            const FVector fwd = E::GetActorForwardVector(box);
            const FVector right{-fwd.Y, fwd.X, 0.f};
            const FVector dirs[4] = {fwd, {-fwd.X, -fwd.Y, 0.f}, right, {-right.X, -right.Y, 0.f}};
            s_standAt = FVector{b.X + dirs[s_side].X * 200.f, b.Y + dirs[s_side].Y * 200.f, b.Z + 120.f};
            E::SetActorLocation(me, s_standAt);
            UE_LOGI("[SIT] host: stands on side %d at (%.0f,%.0f,%.0f)", s_side, s_standAt.X, s_standAt.Y, s_standAt.Z);
            g_placed = true;
            static bool s_lit = false;
            if (!s_lit) { s_lit = true; LightUp(me); }
        }
        ++s_side;
        s_nextSide = t + 2000u;
    }
    if (g_placed && t < 50000u) E::SetControlRotation(E::GetController(me), coop::director::LookAt(E::GetCameraLocation(), FVector{at.X, at.Y, at.Z + 60.f}));
    // VOTVCOOP_SIT_PROBE_GRAB=1: at 52 s the host takes the chair the body sits on in hand.
    static std::unique_ptr<coop::director::AimedGrab> s_grab;
    static bool s_grabDone = false;
    if (!s_grabDone && t >= 52000u && coop::config::ReadEnv("VOTVCOOP_SIT_PROBE_GRAB") == "1") {
        if (!s_grab) {
            void* box = NearestBox(at);
            void* chair = nullptr;
            if (box) E::GetAttachParentActor(box, chair);
            if (chair) s_grab = std::make_unique<coop::director::AimedGrab>(me, chair);
            else s_grabDone = true;
            UE_LOGI("[SIT] host: grabs the chair %p", chair);
        } else if (const auto st = s_grab->Tick(); st != coop::director::GrabState::Working) {
            s_grabDone = true;
            UE_LOGI("[SIT] host: the chair grab %s", st == coop::director::GrabState::Grabbed ? "GRABBED" : "FAILED");
        }
    }
    if (!g_shot1 && t >= 48000u) g_shot1 = harness::screenshot::Capture(L"sit-host-seated") || true;
    if (!g_shot2 && t >= 80000u) g_shot2 = harness::screenshot::Capture(L"sit-host-stood") || true;
    if (t >= 30000u && t < 90000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 2000;
        bool seatPose = false;
        ue_wrap::puppet::GetOnAtv(body, seatPose);
        UE_LOGI("[SIT] host t=%llu: client body (%.0f,%.0f,%.0f) seatPose %d", t / 1000ull, at.X, at.Y, at.Z,
                seatPose ? 1 : 0);
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[SIT] probe armed role=%s", host ? "host" : "client");
    }
    const uint64_t t = Now() - g_startAt;
    if (HostSits()) {
        if (host) { ClientTick(t); WatchTick(t, true); }
        else WatchTick(t, false);
        return;
    }
    if (host) HostTick(t);
    else ClientTick(t);
}

}  // namespace coop::dev::sit_probe
