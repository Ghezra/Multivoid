// coop/props/bucket_pour.cpp -- see coop/props/bucket_pour.h.
//
// 0.9.0n. prop_bucket_C::ReceiveTick, while its staticmesh simulates: calcPoints, `height` cut to what the tipped rim holds,
// interpFlow and upd; when `height_interp` and `height` differ and it is not `empty`, a 0.1 s delay, then the wade sound and
// ftrunc(|height_interp - height| / 2) + 1 prop_sponge_bucketpour_C spawned at the `pour` component, each given the bucket's
// velocity / 2 + a random unit vector * 50 + the bucket's up * (a height term) through its staticmesh. The pour blob's
// collided overlaps a sphere at the hit: extinguishFire on every int_objects actor, shortCircuit on a screen, and
// cleanSponge(getStrength * speed / mass) on every int_objects actor (grime_C among them).

#include "coop/props/bucket_pour.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/props/bucket_sync.h"
#include "coop/props/prop_damage_gate.h"
#include "coop/props/prop_drive_host.h"
#include "coop/props/remote_prop.h"
#include "ue_wrap/actors/bucket.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_physics.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace coop::bucket_pour {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace sg = ue_wrap::script_gate;
namespace EL = coop::element;
namespace B  = ue_wrap::bucket;
namespace CR = ue_wrap::class_resolve;

UE_FACT_FN(kRowBorn, "bucket_pour", L"prop_sponge_bucketPour_C", L"ReceiveBeginPlay", Declared);
constexpr int kTagBorn = 0x42505201;  // 'BPR'

constexpr const wchar_t* kBlobClass = L"prop_sponge_bucketPour_C";
constexpr const wchar_t* kBuckets[] = {L"prop_bucket_C", L"prop_bucket_mop_C"};
constexpr float kNearCm = 150.f;      // CLIENT: a blob this near a bucket this client moves is its pour
constexpr float kHostNearCm = 300.f;  // HOST: a blob this near the host's copy of the bucket (the rows lag the hand)
constexpr float kMaxSpeed = 5000.f;   // HOST: a blob faster than this is no pour's
constexpr float kPerBlob = 2.f;       // the least water one blob takes: the tick spawns one per 2 of the eased drop
constexpr float kMaxPerBlob = 6.f;    // the most: the rim's cut empties more than the eased drop's blobs (a full bucket
                                      // turned over spawns 12 for its 36)
constexpr double kBlobsPerSec = 60.0; // HOST: a sender's budget, a full bucket's pour in about a second
constexpr double kBurst = 60.0;
constexpr int kCap = 32;

N::Session* g_session = nullptr;
bool g_watched = false;
bool g_settled = false;
Counts g_counts;

// CLIENT: the blobs born this frame, judged next frame (a host blob's copy is bound by then).
struct Born {
    ue_wrap::CachedObjRef blob;
    ue_wrap::CachedObjRef bucket;
};
std::vector<Born> g_born;

// HOST: per sender, a token bucket of blobs.
struct Budget {
    double tokens = kBurst;
    uint64_t at = 0;
};
Budget g_budget[N::kMaxPeers];

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }
uint64_t NowMs() { return ::GetTickCount64(); }

bool IsBucket(void* obj) {
    if (!obj || !R::IsLive(obj)) return false;
    for (const wchar_t* c : kBuckets)
        if (R::NameEquals(R::NameOf(R::ClassOf(obj)), c)) return true;
    return false;
}

uint32_t EidOf(void* obj) {
    const EL::ElementId eid = EL::Registry::Get().EidForActor(obj);
    return eid == EL::kInvalidId ? 0u : static_cast<uint32_t>(eid);
}

float Dist2(const ue_wrap::FVector& a, const ue_wrap::FVector& b) {
    return (a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z);
}

// CLIENT: the bucket this client moves whose blob this is, or null.
void* PouringBucket(void* blob) {
    ue_wrap::FVector at{};
    if (!E::TryGetActorLocation(blob, at)) return nullptr;
    void* found[kCap];
    for (const wchar_t* c : kBuckets) {
        const int n = ue_wrap::world_instances::Find(c, found, kCap);
        for (int i = 0; i < n; ++i) {
            ue_wrap::FVector b{};
            if (IsBucket(found[i]) && E::TryGetActorLocation(found[i], b) && Dist2(at, b) <= kNearCm * kNearCm &&
                coop::props::prop_damage_gate::AuthoredHere(found[i]))
                return found[i];
        }
    }
    return nullptr;
}

// CLIENT: a pour blob was born; it is judged next frame.
void OnBornPost(const sg::Call& c) {
    if (!IsClient() || !c.object) return;
    void* const bucket = PouringBucket(c.object);
    if (!bucket || g_born.size() >= 64) return;
    g_born.emplace_back();
    g_born.back().blob.Set(c.object);
    g_born.back().bucket.Set(bucket);
}

// CLIENT: last frame's blobs of this client's pour, to the host; each removed here, since the host's spawn crosses back.
void SendBorn() {
    if (g_born.empty()) return;
    N::BucketPourAskPayload p{};
    auto flush = [&]() {
        if (!p.count) return;
        if (g_session->SendReliable(N::ReliableKind::BucketPourAsk, &p, sizeof(p))) g_counts.asked += p.count;
        p.count = 0;
    };
    for (const Born& b : g_born) {
        void* const blob = b.blob.Get();
        void* const bucket = b.bucket.Get();
        if (!blob || !bucket || EidOf(blob)) continue;  // a host blob's copy: bound to its element
        const uint32_t eid = EidOf(bucket);
        if (!eid) continue;
        if (p.count && p.eid != eid) flush();
        p.eid = eid;
        B::Plain cw;
        p.level = B::ReadPlain(bucket, cw) ? cw.height : -1.f;  // the copy's level after its pour
        ue_wrap::FVector at{}, lin{}, ang{};
        if (!E::TryGetActorLocation(blob, at)) continue;
        E::GetActorRootPhysicsVelocity(blob, lin, ang);
        N::BucketPourBlob& o = p.blobs[p.count++];
        o.px = at.X;
        o.py = at.Y;
        o.pz = at.Z;
        o.vx = lin.X;
        o.vy = lin.Y;
        o.vz = lin.Z;
        E::DestroyActor(blob);
        if (p.count == N::kBucketPourMaxBlobs) flush();
    }
    flush();
    g_born.clear();
}

bool Finite(const N::BucketPourBlob& b) {
    return std::isfinite(b.px) && std::isfinite(b.py) && std::isfinite(b.pz) && std::isfinite(b.vx) &&
           std::isfinite(b.vy) && std::isfinite(b.vz);
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    if (!sg::WatchName(kRowBorn, kTagBorn, nullptr, &OnBornPost))
        UE_LOGE("bucket_pour: the watch on a pour blob's birth was refused");
}

void Tick() {
    if (IsClient()) SendBorn();
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowBorn, kTagBorn)) return;
    g_settled = true;
    if (sg::NameWatchLive(kRowBorn, kTagBorn)) UE_LOGI("bucket_pour: the watch on a pour blob's birth is live");
    else UE_LOGE("bucket_pour: the watch on a pour blob's birth is not live -- a client's pour stays on its machine");
}

void OnAsk(int senderSlot, const N::BucketPourAskPayload& p) {
    if (!IsHost() || senderSlot <= 0 || senderSlot >= static_cast<int>(N::kMaxPeers)) return;
    void* const bucket = EL::LiveActorOfType(static_cast<EL::ElementId>(p.eid), EL::ElementType::Prop);
    // The sender moves it: its grab's drive, or its syncer row (a push).
    const bool moves = IsBucket(bucket) && (coop::remote_prop::IsDrivenBy(bucket, senderSlot) ||
                                            coop::prop_drive_host::ClientSyncerOf(p.eid) == senderSlot);
    ue_wrap::FVector at{};
    B::Plain w;
    if (!IsBucket(bucket) || !moves || !E::TryGetActorLocation(bucket, at) || !B::ReadPlain(bucket, w)) {
        g_counts.refused += p.count;
        UE_LOGW("bucket_pour: HOST slot %d's pour of %u blobs from eid %u refused (%s)", senderSlot, p.count, p.eid,
                !IsBucket(bucket) ? "no bucket" : !moves ? "the bucket is not in the sender's grab" : "unread");
        return;
    }
    Budget& bg = g_budget[senderSlot];
    const uint64_t now = NowMs();
    bg.tokens = std::fmin(kBurst, bg.tokens + static_cast<double>(now - bg.at) / 1000.0 * kBlobsPerSec);
    bg.at = now;
    const CR::Resolved rc = CR::ResolveClass(kBlobClass, CR::AnyFamily(), CR::Load::Immediate);
    int spawned = 0, refused = 0;
    for (uint8_t i = 0; i < p.count; ++i) {
        const N::BucketPourBlob& b = p.blobs[i];
        const ue_wrap::FVector pos{b.px, b.py, b.pz};
        const float speed2 = b.vx * b.vx + b.vy * b.vy + b.vz * b.vz;
        if (!rc.cls || !Finite(b) || Dist2(pos, at) > kHostNearCm * kHostNearCm || speed2 > kMaxSpeed * kMaxSpeed ||
            bg.tokens < 1.0 || w.height < kPerBlob) {
            ++refused;
            continue;
        }
        void* const a = E::BeginDeferredSpawn(rc.cls, pos, {});
        if (!a || !E::FinishDeferredSpawn(a, pos, {})) {
            ++refused;
            continue;
        }
        E::SetComponentLinearVelocity(E::RootComponentOf(a), b.vx, b.vy, b.vz);
        bg.tokens -= 1.0;
        w.height -= kPerBlob;
        ++spawned;
    }
    g_counts.spawned += static_cast<unsigned>(spawned);
    g_counts.refused += static_cast<unsigned>(refused);
    if (spawned) {
        // The client's copy's level, where it lies between the least and the most its blobs may take.
        const float most = w.height + 2.f * static_cast<float>(spawned) - kMaxPerBlob * static_cast<float>(spawned);
        if (std::isfinite(p.level) && p.level >= 0.f && p.level < w.height) w.height = p.level < most ? most : p.level;
        B::WritePlain(bucket, w.height < 0.f ? 0.f : w.height, w.soap, w.alcoholP, w.alcohol, w.alcReady);
        // The bucket in a client's hand is a copy here, so its own tick never runs to repaint what the write
        // changed: without the game's own repaint the water mesh keeps the level it showed before the pour (a
        // bucket emptying on the client stayed visibly full on the host). bucket_sync's Apply repaints the same
        // way; this write is the pour's, so it owes the same call.
        B::CallUpd(bucket);
        coop::bucket_sync::MarkDirty(bucket);
    }
    UE_LOGI("bucket_pour: HOST slot %d poured %d blobs from bucket eid %u (%d refused), the first at (%.0f,%.0f,%.0f) going "
            "(%.0f,%.0f,%.0f); its water %.1f", senderSlot, spawned, p.eid, refused, p.blobs[0].px, p.blobs[0].py,
            p.blobs[0].pz, p.blobs[0].vx, p.blobs[0].vy, p.blobs[0].vz, w.height < 0.f ? 0.f : w.height);
}

Counts GetCounts() { return g_counts; }

void OnDisconnect() {
    if (g_counts.asked || g_counts.spawned || g_counts.refused)
        UE_LOGI("bucket_pour: session end -- client blobs asked %u; host blobs spawned %u, refused %u", g_counts.asked,
                g_counts.spawned, g_counts.refused);
    g_counts = Counts{};
    g_born.clear();
    for (Budget& b : g_budget) b = Budget{};
}

}  // namespace coop::bucket_pour
