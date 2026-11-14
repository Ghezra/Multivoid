// coop/dev/wall_fill_probe.cpp -- [dev] an unfinished wall filled with bricks by both players (VOTVCOOP_WALL_FILL_PROBE=1).
//
// HOST, after a client is connected: at 20 s places a customWall_unfinished_C 3 m before its player, as the wall
// builder's addWall places one (shape and material set before it finishes spawning), its requirement made 3 so the
// client's brick finishes it; at 24 s and 26 s spawns a brick
// (the catalog's `brick` row, through the gamemode's spawnPropThroughGamemode) in the wall, as one dropped in lands;
// at 30 s spawns a brick 1 m before the client's puppet. CLIENT: from 33 s grabs the nearest brick through the use key's
// own grab chain (the director's aimed grab), and from 38 s to 40 s carries it into the wall. BOTH, every second from
// 20 s to 60 s: every unfinished wall's key, bricks, requirement and type, the bricks in the world and the finished
// walls (`[FILL]`).

#include "coop/dev/wall_fill_probe.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/world/world_singleton.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace coop::dev::wall_fill_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace OI = ue_wrap::object_index;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool on = coop::config::ReadEnv("VOTVCOOP_WALL_FILL_PROBE") == "1";
    return on;
}

std::vector<void*> All(const wchar_t* cls) {
    std::vector<void*> out;
    void* const c = OI::ClassByName(cls);
    if (c)
        OI::ForEachInstance(c, [](void* ctx, void* obj, int32_t) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__"))
                static_cast<std::vector<void*>*>(ctx)->push_back(obj);
        }, &out);
    return out;
}

int32_t Int(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? *reinterpret_cast<const int32_t*>(static_cast<const char*>(obj) + off) : -999;
}

void SetInt(void* obj, const wchar_t* name, int32_t v) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off >= 0) *reinterpret_cast<int32_t*>(static_cast<char*>(obj) + off) = v;
}

std::wstring Str(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off >= 0 ? ue_wrap::field_io::ReadFStringAt(obj, off) : L"?";
}

std::wstring PropName(void* prop) {
    const int32_t off = prop ? R::FindPropertyOffset(R::ClassOf(prop), L"Name") : -1;
    return off >= 0 ? R::ToString(*reinterpret_cast<const R::FName*>(static_cast<const char*>(prop) + off)) : L"";
}

// The world's props named `brick`.
std::vector<void*> Bricks() {
    std::vector<void*> out;
    for (void* p : All(L"prop_C"))
        if (_wcsicmp(PropName(p).c_str(), L"brick") == 0) out.push_back(p);
    return out;
}

void* Wall() {
    std::vector<void*> w = All(L"customWall_unfinished_C");
    return w.empty() ? nullptr : w.back();
}

bool SpawnBrick(const ue_wrap::FVector& at) {
    void* const gm = ue_wrap::world_singleton::Gamemode();
    ue_wrap::ParamFrame f(gm ? R::FindDispatchFunctionCached(R::ClassOf(gm), L"spawnPropThroughGamemode") : nullptr);
    if (!f.valid()) return false;
    const R::FName row = ue_wrap::fname_utils::StringToFName(L"brick");
    ue_wrap::FTransform xf{};
    xf.TX = at.X;
    xf.TY = at.Y;
    xf.TZ = at.Z;
    return f.SetRaw(L"prop", &row, sizeof(row)) && f.SetRaw(L"InputPin", &xf, sizeof(xf)) &&
           f.Set<int32_t>(L"amount", 1) && ue_wrap::Call(gm, f);
}

void* PlaceWall() {
    void* const me = coop::players::Registry::Get().Local();
    void* const cls = ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/customWall_unfinished.customWall_unfinished_C");
    ue_wrap::FVector at{};
    if (!me || !cls || !E::TryGetActorLocation(me, at)) return nullptr;
    const ue_wrap::FVector fwd = E::GetActorForwardVector(me);
    at = {at.X + fwd.X * 300.f, at.Y + fwd.Y * 300.f, at.Z + 20.f};
    void* const w = E::BeginDeferredSpawn(cls, at, {});
    if (!w) return nullptr;
    SetInt(w, L"Shape", 0);
    SetInt(w, L"Material", 0);
    return E::FinishDeferredSpawn(w, at, {}) ? w : nullptr;
}

void Report() {
    for (void* w : All(L"customWall_unfinished_C"))
        UE_LOGI("[FILL] wall key=%ls bricks=%d requirement=%d type='%ls'", ue_wrap::prop::GetInteractableKeyString(w).c_str(),
                Int(w, L"bricks"), Int(w, L"requirement"), Str(w, L"wallType").c_str());
    UE_LOGI("[FILL] bricks in the world=%zu finished walls=%zu wet=%zu", Bricks().size(), All(L"customWall_C").size(),
            All(L"customWall_wetConcrete_C").size());
}

uint64_t g_startAt = 0, g_reportAt = 0;
int g_step = 0;
coop::director::AimedGrab* g_grab = nullptr;
bool g_grabbed = false;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[FILL] probe armed role=%s", host ? "host" : "client");
    }
    const uint64_t t = Now() - g_startAt;
    void* const me = coop::players::Registry::Get().Local();
    ue_wrap::FVector wallAt{};
    void* const wall = Wall();
    if (wall) E::TryGetActorLocation(wall, wallAt);
    if (host && g_step == 0 && t >= 20000u) {
        g_step = 1;
        void* const w = PlaceWall();
        const int32_t need = Int(w, L"requirement");
        SetInt(w, L"requirement", 3);  // three bricks finish it: the host's two and the client's one
        UE_LOGI("[FILL] host: placed an unfinished wall (ok=%d requirement=%d, made 3)", w ? 1 : 0, need);
    }
    if (host && (g_step == 1 || g_step == 2) && t >= (g_step == 1 ? 24000u : 26000u)) {
        ++g_step;
        UE_LOGI("[FILL] host: dropped a brick into the wall (ran=%d)", wall && SpawnBrick({wallAt.X, wallAt.Y, wallAt.Z + 30.f}) ? 1 : 0);
    }
    if (host && g_step == 3 && t >= 30000u) {
        g_step = 4;
        ue_wrap::FVector at{};
        bool ran = false;
        for (uint8_t s = 1; s < coop::net::kMaxPeers && !ran; ++s) {
            coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(s);
            if (rp && rp->valid() && rp->TryGetLocation(at)) ran = SpawnBrick({at.X + 80.f, at.Y, at.Z + 20.f});
        }
        UE_LOGI("[FILL] host: put a brick before the client (ran=%d)", ran ? 1 : 0);
    }
    if (!host && !g_grabbed && t >= 33000u && t < 38000u) {
        void* nearest = nullptr;
        float best = 1e30f;
        ue_wrap::FVector m{};
        if (me && E::TryGetActorLocation(me, m))
            for (void* b : Bricks()) {
                ue_wrap::FVector p{};
                if (!E::TryGetActorLocation(b, p)) continue;
                const float d = (p.X - m.X) * (p.X - m.X) + (p.Y - m.Y) * (p.Y - m.Y);
                if (d < best) { best = d; nearest = b; }
            }
        if (!g_grab && nearest && me) g_grab = new coop::director::AimedGrab(me, nearest);
        if (g_grab) {
            const coop::director::GrabState g = g_grab->Tick();
            if (g != coop::director::GrabState::Working) {
                UE_LOGI("[FILL] client: grabbed a brick (%hs%hs)", g == coop::director::GrabState::Grabbed ? "ok" : "failed: ",
                        g == coop::director::GrabState::Grabbed ? "" : g_grab->Why());
                delete g_grab;
                g_grab = nullptr;
                g_grabbed = true;
            }
        }
    }
    if (!host && t >= 38000u && t < 40000u && wall) {  // the held brick carried into the wall
        ue_wrap::engine::MainPlayerGrabState gs{};
        if (ue_wrap::engine::ReadMainPlayerGrabState(me, gs) && gs.grabbingActor && R::IsLive(gs.grabbingActor)) {
            static bool s_said = false;
            if (!s_said) UE_LOGI("[FILL] client: carries the brick it holds into the wall");
            s_said = true;
            E::SetActorLocation(gs.grabbingActor, {wallAt.X, wallAt.Y, wallAt.Z + 30.f});
        }
    }
    if (t < 20000u || t > 60000u || Now() < g_reportAt) return;
    g_reportAt = Now() + 1000;
    Report();
}

}  // namespace coop::dev::wall_fill_probe
