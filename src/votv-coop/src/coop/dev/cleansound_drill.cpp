// coop/dev/cleansound_drill.cpp -- [dev] the cleaning sounds on the sound relay. See the header.

#include "coop/dev/cleansound_drill.h"

#include "clean_drill_detail.h"  // Clean: a stroke's call of grime_C::clean

#include "coop/config/config.h"
#include "coop/dev/director/routes.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/world/sound_catalog.h"
#include "coop/world/sound_relay.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/devices/grime.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace coop::dev::cleansound_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using ue_wrap::FVector;

uint64_t Now() { return ::GetTickCount64(); }

bool Armed() {
    static const bool v = !coop::config::ReadEnv("VOTVCOOP_CLEANSOUND_DRILL").empty();
    return v;
}

uint64_t g_startAt = 0;
int g_step = 0;
int g_strokes = 0;
uint64_t g_strokeAt = 0;
void* g_sponge = nullptr;
void* g_decal = nullptr;
FVector g_bucketAt{};

float Dist(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void* Nearest(const wchar_t* cls, const FVector& at, float& d) {
    void* best = nullptr;
    d = 1e30f;
    for (void* o : R::FindObjectsByClass(cls)) {
        FVector p{};
        if (!o || !R::IsLive(o) || R::NameStartsWith(R::NameOf(o), L"Default__") || !E::TryGetActorLocation(o, p)) continue;
        const float x = Dist(p, at);
        if (x < d) {
            d = x;
            best = o;
        }
    }
    return best;
}

// The grime decal nearest `at` among the save's cleanable kinds.
void* NearestDecal(const FVector& at, float& d) {
    static const wchar_t* const kKinds[] = {L"grime_dusty_C", L"grime_leaky_C", L"grime_coffee_C", L"grime_grainy_C",
                                            L"grime_blood2_C", L"grime_blood2_drip_C", L"grime_dyn_C", L"grime_beer_C"};
    void* best = nullptr;
    d = 1e30f;
    for (const wchar_t* k : kKinds) {
        float x = 0.f;
        void* const o = Nearest(k, at, x);
        if (o && x < d && ue_wrap::grime::IsGrime(o)) {
            d = x;
            best = o;
        }
    }
    return best;
}

void TickActor(uint64_t t) {
    void* const me = coop::players::Registry::Get().Local();
    if (!me) return;
    if (g_step == 0 && t >= 40000u) {
        g_step = 1;
        void* const cls = ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/prop_sponge.prop_sponge_C");
        FVector at{};
        E::TryGetActorLocation(me, at);
        void* const sponge = cls ? E::SpawnActor(cls, {at.X, at.Y, at.Z + 60.f}) : nullptr;
        bool collected = false;
        const bool held = sponge && E::CallMainPlayerHoldObject(me, sponge, collected) && collected;
        E::MainPlayerGrabState hand{};
        g_sponge = held && E::ReadMainPlayerGrabState(me, hand) ? hand.holdingActor : nullptr;
        UE_LOGI("[CSND] actor: a sponge in its hand (held=%d)", g_sponge ? 1 : 0);
    }
    if (g_step == 1 && t >= 46000u) {
        g_step = 2;
        // The bucket to the actor, not the actor to the bucket: a body stood by its corner of the base is moved off the
        // map by the clip check. The save's water bucket goes a metre ahead of the actor, on its floor, and the view
        // turns down onto it.
        FVector at{}, b{};
        E::TryGetActorLocation(me, at);
        float d = 0.f;
        void* const bucket = Nearest(L"prop_bucket_C", at, d);
        void* const pc = E::GetController(me);
        const ue_wrap::FRotator view = pc ? E::GetControlRotation(pc) : ue_wrap::FRotator{};
        const float yawRad = view.Yaw * 0.0174532925f;
        const FVector to{at.X + 100.f * std::cos(yawRad), at.Y + 100.f * std::sin(yawRad), at.Z - 80.f};
        const bool moved = bucket && E::SetActorLocation(bucket, to);
        if (pc) E::SetControlRotation(pc, ue_wrap::FRotator{-54.f, view.Yaw, 0.f});   // the eye 1.4 m over it, 1 m back
        E::TryGetActorLocation(bucket, b);
        g_bucketAt = b;
        // Water in it: the sponge dips only into a bucket whose `Height` holds some.
        const int32_t hOff = bucket ? R::FindPropertyOffset(R::ClassOf(bucket), L"Height") : -1;
        if (hOff >= 0) {
            float& h = *reinterpret_cast<float*>(static_cast<uint8_t*>(bucket) + hOff);
            UE_LOGI("[CSND] actor: the bucket's water %.2f -> 1.00", h);
            if (h < 1.f) h = 1.f;
        }
        FVector mine{};
        E::TryGetActorLocation(me, mine);
        UE_LOGI("[CSND] actor: stands at (%.0f,%.0f,%.0f)", mine.X, mine.Y, mine.Z);
        UE_LOGI("[CSND] actor: the water bucket from %.0f cm off to a metre ahead (moved=%d, at %.0f,%.0f,%.0f)", d,
                moved ? 1 : 0, b.X, b.Y, b.Z);
    }
    if (g_step == 2 && t >= 48000u) {
        g_step = 3;
        void* const fn = g_sponge ? R::FindFunction(R::ClassOf(g_sponge), L"playerHandUse_LMB") : nullptr;
        ue_wrap::ParamFrame f(fn);
        bool ran = false;
        if (f.valid()) {
            f.Set<void*>(L"player", me);
            ran = ue_wrap::Call(g_sponge, f);
        }
        UE_LOGI("[CSND] actor: the sponge's use into the bucket (ran=%d)", ran ? 1 : 0);
    }
    if (g_step == 3 && t >= 53000u) {
        FVector at{};
        E::TryGetActorLocation(me, at);
        if (!g_decal) {
            float d = 0.f;
            g_decal = NearestDecal(g_bucketAt, d);   // the decal nearest the bucket: the base's
            UE_LOGI("[CSND] actor: the nearest decal %ls, %.0f cm off", g_decal ? R::ClassNameOf(g_decal).c_str() : L"-",
                    d);
            if (!g_decal) {
                g_step = 4;
                return;
            }
            FVector p{};
            E::TryGetActorLocation(g_decal, p);
            UE_LOGI("[CSND] actor: that decal is %.0f cm from its player", Dist(p, at));
        }
        if (Now() - g_strokeAt >= 500u && g_strokes < 5) {
            g_strokeAt = Now();
            const bool ok = g_decal && R::IsLive(g_decal) &&
                            coop::dev::clean_drill::detail::Clean(g_decal, g_sponge, 6.f);
            ++g_strokes;
            UE_LOGI("[CSND] actor: stroke %d (ok=%d)", g_strokes, ok ? 1 : 0);
        }
        if (g_strokes >= 5) g_step = 4;
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!Armed() || !session || !session->running() || !session->connected()) return;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[CSND] drill armed slot=%d", slot);
    }
    const uint64_t t = Now() - g_startAt;
    // Every peer's table built before the work, so the first sound finds it (the build waits for the loaded sounds).
    if (t >= 30000u) coop::sound_catalog::Ready();
    if (slot == 0) TickActor(t);
    static bool s_done = false;
    if (!s_done && t >= 75000u) {
        s_done = true;
        const coop::sound_relay::Counts c = coop::sound_relay::GetCounts();
        unsigned refused = 0;
        for (unsigned r : c.refused) refused += r;
        UE_LOGI("[CSND] done slot=%d: played here %u, sent %u, not catalogued %u, not ready %u, played from a peer %u, "
                "refused %u", slot, c.playedHere, c.sent, c.notCatalogued, c.notReadyHere, c.played, refused);
    }
}

}  // namespace coop::dev::cleansound_drill
