// coop/props/photo_sync.cpp -- see coop/props/photo_sync.h.
//
// 0.9.0n. prop_digcam_C: playerHandUse_LMB (and actionOptionIndex action 4, its own arrow's view) calls the gamemode's
// `photo(location, rotation, flash)`, which after two frames appends the capture (a JPEG, quality 85) to
// saveSlot.photos, and then makePhoto: with `canPrint`, a 0.2 s delay, checkPaper (a paper taken from the pocket, true)
// and then photos_phys[last] = photos[last], a prop_photo_C spawned at the player with index and originalIndex = last,
// its loadImage, and putObjectInventory2; without a paper the hint "No paper found!". prop_photo_C: loadImage reads
// photos_phys[index] into its material; loadData reads `index` from the record's ints and calls loadImage.

#include "coop/props/photo_sync.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/blob_chunks.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/photo_store.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/class_resolve.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace coop::photo_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace sg = ue_wrap::script_gate;
namespace EL = coop::element;
namespace BC = coop::blob_chunks;
namespace CR = ue_wrap::class_resolve;
namespace PS = ue_wrap::photo_store;

UE_FACT_FN(kRowMake, "photo_sync", L"prop_digcam_C", L"makePhoto", Declared);
UE_FACT_FN(kRowPaper, "photo_sync", L"prop_digcam_C", L"checkPaper", Declared);
UE_FACT_FN(kRowLoad, "photo_sync", L"prop_photo_C", L"loadImage", Declared);
constexpr int kTagMake = 0x50484F01, kTagPaper = 0x50484F02, kTagLoad = 0x50484F03;  // 'PHO'

constexpr const wchar_t* kCamClass = L"prop_digcam_C", *kPrintCamClass = L"prop_digcam_print_C";  // the store's two
constexpr const wchar_t* kPhotoClass = L"prop_photo_C";
constexpr uint8_t kOpPrint = 1, kOpImage = 2;
constexpr uint8_t kNoGrant = 0xFF;
constexpr size_t kHead = 16;                 // [op][grant][part][parts][u32 id][u32 camera][u32 total]
constexpr size_t kPart = 50000;              // a part's picture bytes: one blob, under the blob ceiling undeflated
constexpr size_t kMaxTotal = 32 * kPart;     // a picture past 1.6 MB is no camera's
constexpr uint64_t kDelayMs = 250;           // CLIENT: the game's own 0.2 s, and the capture's two frames
constexpr uint64_t kPrintGapMs = 1000;       // HOST: one print a second from a sender
constexpr uint64_t kQuietMs = 600;           // HOST: its own makePhoto's 0.2 s and its capture's frames, and margin
constexpr float kReachCm = 600.f;            // HOST: the camera to the printer's body
constexpr std::chrono::seconds kTtl{30};
constexpr int kCap = 256;
static_assert(kHead + kPart <= BC::MaxBlobBytes(), "a picture's part must fit one blob");

N::Session* g_session = nullptr;
bool g_watched = false;
bool g_settled = false;
void* g_applying = nullptr;  // CLIENT: the camera whose makePhoto runs as the game's own
void* g_noPaper = nullptr;   // CLIENT: the camera whose next checkPaper answers no paper (the hint's run)
uint32_t g_blobSeq = 1;
uint32_t g_printSeq = 1;
BC::Assembler g_asm;
std::chrono::steady_clock::time_point g_nextSweep{};
void* g_found[kCap];

// CLIENT: a camera's print, due 0.25 s after its makePhoto.
struct Due {
    ue_wrap::CachedObjRef cam;
    uint64_t at = 0;
};
std::vector<Due> g_due;

// A picture's parts, keyed by sender, op and id.
struct Parts {
    uint8_t grant = kNoGrant, parts = 0, got = 0;
    uint32_t cam = 0, total = 0;
    std::vector<bool> have;
    std::vector<uint8_t> bytes;
    std::chrono::steady_clock::time_point started{};
};
std::map<std::tuple<uint8_t, uint8_t, uint32_t>, Parts> g_parts;

std::set<int32_t> g_published;            // HOST: the indices whose picture went to every client this session
int32_t g_lastClientPrint = -1;           // HOST: the index the last client print was filed at (the drill reads it)

// HOST: its own capture appends to the gallery at once and its makePhoto prints the gallery's last entry 0.2 s on, so
// a client's print filed between the two would be printed as the host's. A client's print waits out that span.
struct Held {
    int slot = 0;
    uint32_t cam = 0;
    std::vector<uint8_t> pic;
};
std::vector<Held> g_held;
uint64_t g_quietUntil = 0;
uint64_t g_lastPrintAt[N::kMaxPeers] = {};  // HOST

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }
uint64_t NowMs() { return ::GetTickCount64(); }

bool IsA(void* obj, const wchar_t* cls) { return obj && R::IsLive(obj) && R::NameEquals(R::NameOf(R::ClassOf(obj)), cls); }
bool IsCam(void* obj) { return IsA(obj, kCamClass) || IsA(obj, kPrintCamClass); }

uint32_t EidOf(void* obj) {
    const EL::ElementId eid = EL::Registry::Get().EidForActor(obj);
    return eid == EL::kInvalidId ? 0u : static_cast<uint32_t>(eid);
}

R::InstanceOffset s_index(L"index"), s_original(L"originalIndex");
int32_t* IntAt(void* obj, R::InstanceOffset& o) {
    const int32_t off = obj ? o.Of(obj) : -1;
    return off >= 0 ? reinterpret_cast<int32_t*>(static_cast<char*>(obj) + off) : nullptr;
}

bool CanPrint(void* cam) {
    int32_t off = -1;
    uint8_t mask = 0;
    if (!cam || !R::FindBoolProperty(R::ClassOf(cam), L"canPrint", off, mask) || off < 0) return false;
    return (*(static_cast<uint8_t*>(cam) + off) & mask) != 0;
}

bool CallNoArgs(void* obj, const wchar_t* fn) {
    void* const f = obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), fn) : nullptr;
    return f && R::CallFunction(obj, f, nullptr);
}

void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
uint32_t Get32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

// A picture in parts: to every peer, or to one slot (`slot` >= 0).
bool SendPicture(uint8_t op, uint8_t grant, uint32_t id, uint32_t cam, const std::vector<uint8_t>& pic, int slot = -1) {
    if (!g_session || !g_session->connected() || pic.empty() || pic.size() > kMaxTotal) return false;
    const size_t parts = (pic.size() + kPart - 1) / kPart;
    bool ok = true;
    for (size_t i = 0; i < parts; ++i) {
        const size_t from = i * kPart, n = pic.size() - from < kPart ? pic.size() - from : kPart;
        std::vector<uint8_t> blob(kHead + n);
        blob[0] = op;
        blob[1] = grant;
        blob[2] = static_cast<uint8_t>(i);
        blob[3] = static_cast<uint8_t>(parts);
        Put32(blob, 4, id);
        Put32(blob, 8, cam);
        Put32(blob, 12, static_cast<uint32_t>(pic.size()));
        std::memcpy(blob.data() + kHead, pic.data() + from, n);
        ok = (slot >= 0 ? BC::SendBlobToSlot(g_session, slot, N::ReliableKind::PhotoImage, g_blobSeq++, blob)
                        : BC::SendBlob(g_session, N::ReliableKind::PhotoImage, g_blobSeq++, blob)) && ok;
    }
    return ok;
}

// Every photo copy here of `index` shows its picture again.
int Reload(int32_t index) {
    const int n = ue_wrap::world_instances::Find(kPhotoClass, g_found, kCap);
    int reloaded = 0;
    for (int i = 0; i < n; ++i) {
        const int32_t* const idx = IntAt(g_found[i], s_index);
        if (IsA(g_found[i], kPhotoClass) && idx && *idx == index && CallNoArgs(g_found[i], L"loadImage")) ++reloaded;
    }
    return reloaded;
}

// HOST: the picture of `index` to every client, once a session.
void Publish(int32_t index, uint8_t grant) {
    std::vector<uint8_t> pic;
    if (!IsHost() || !g_session->connected() || index < 0 || !PS::Read(true, index, pic)) return;
    if (grant == kNoGrant && !g_published.insert(index).second) return;
    g_published.insert(index);
    const bool ok = SendPicture(kOpImage, grant, static_cast<uint32_t>(index), 0, pic);
    UE_LOGI("photo_sync: HOST photo %d's picture (%zu bytes) sent to every client%s (ok=%d)", index, pic.size(),
            grant == kNoGrant ? "" : ", a client's print", ok ? 1 : 0);
}

// ---- the gate ----------------------------------------------------------------------------------------------------------
// CLIENT: the print is run here 0.25 s on, so the host's save names it.
sg::Verdict OnMakePre(const sg::Call& c) {
    if (IsHost()) g_quietUntil = NowMs() + kQuietMs;  // its own print takes the gallery's last entry: hold a client's
    if (!IsClient() || !c.object || c.object == g_applying || !CanPrint(c.object) || !EidOf(c.object))
        return sg::Verdict::Run;
    for (const Due& d : g_due)
        if (d.cam.Is(c.object)) return sg::Verdict::Cancel;
    g_due.emplace_back();
    g_due.back().cam.Set(c.object);
    g_due.back().at = NowMs() + kDelayMs;
    return sg::Verdict::Cancel;
}

// CLIENT: the game's makePhoto run for its hint finds no paper.
sg::Verdict OnPaperPre(const sg::Call& c) {
    if (!IsClient() || !c.object || c.object != g_noPaper || !c.function || !c.locals) return sg::Verdict::Run;
    g_noPaper = nullptr;
    static R::ParamOffset s_ret(L"return");
    const int32_t off = s_ret.Of(c.function);
    if (off >= 0) c.locals[off] = 0;
    return sg::Verdict::Cancel;
}

// HOST: every picture its copies show goes to the clients.
void OnLoadPost(const sg::Call& c) {
    if (!IsHost() || !c.object || !g_session->connected()) return;
    if (const int32_t* const idx = IntAt(c.object, s_index)) Publish(*idx, kNoGrant);
}

// CLIENT: a camera's print, due now.
void RunDue() {
    const uint64_t now = NowMs();
    for (size_t i = 0; i < g_due.size();) {
        if (g_due[i].at > now) {
            ++i;
            continue;
        }
        void* const cam = g_due[i].cam.Get();
        g_due.erase(g_due.begin() + static_cast<std::ptrdiff_t>(i));
        if (!IsCam(cam)) continue;
        ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(cam), L"checkPaper"));
        const bool paper = f.valid() && ue_wrap::Call(cam, f) && f.Get<bool>(L"return");
        if (!paper) {
            g_noPaper = cam;
            g_applying = cam;
            CallNoArgs(cam, L"makePhoto");  // the game's own hint
            g_applying = nullptr;
            UE_LOGI("photo_sync: CLIENT camera eid %u found no paper -- nothing printed", EidOf(cam));
            continue;
        }
        std::vector<uint8_t> pic;
        const int32_t last = PS::Count(false) - 1;
        if (!PS::Read(false, last, pic)) {
            UE_LOGW("photo_sync: CLIENT camera eid %u printed, but the gallery's last capture (%d) is unread", EidOf(cam),
                    last);
            continue;
        }
        const bool ok = SendPicture(kOpPrint, kNoGrant, g_printSeq++, EidOf(cam), pic);
        UE_LOGI("photo_sync: CLIENT camera eid %u printed capture %d (%zu bytes) -- asked of the host (ok=%d)", EidOf(cam),
                last, pic.size(), ok ? 1 : 0);
    }
}

// CLIENT: the host's picture is this machine's for its index; the printer pockets its photo.
void TakeImage(uint8_t grant, int32_t index, const std::vector<uint8_t>& pic) {
    if (!PS::WritePhys(index, pic)) {
        UE_LOGW("photo_sync: CLIENT photo %d's picture (%zu bytes) was not written here", index, pic.size());
        return;
    }
    const int reloaded = Reload(index);
    UE_LOGI("photo_sync: CLIENT photo %d's picture (%zu bytes) is the host's here; %d copies show it", index, pic.size(),
            reloaded);
    if (grant != coop::players::Registry::Get().LocalPeerId()) return;
    void* const me = coop::players::Registry::Get().Local();
    const CR::Resolved rc = CR::ResolveClass(kPhotoClass, CR::AnyFamily(), CR::Load::Immediate);
    ue_wrap::FVector at{};
    if (!me || !rc.cls || !E::TryGetActorLocation(me, at)) return;
    void* const a = E::BeginDeferredSpawn(rc.cls, at, {});
    if (int32_t* const i = IntAt(a, s_index)) *i = index;
    if (int32_t* const o = IntAt(a, s_original)) *o = index;
    void* const photo = a && E::FinishDeferredSpawn(a, at, {}) ? a : nullptr;
    if (photo) CallNoArgs(photo, L"loadImage");
    void* const fn = photo ? R::FindDispatchFunctionCached(R::ClassOf(me), L"putObjectInventory2") : nullptr;
    ue_wrap::ParamFrame f(fn);
    const bool stored = fn && f.valid() && f.Set<void*>(L"inputPin", photo) && f.Set<bool>(L"noNotify", false) &&
                        ue_wrap::Call(me, f);
    UE_LOGI("photo_sync: CLIENT this player's print is photo %d, pocketed (spawned %d stored %d)", index, photo ? 1 : 0,
            stored ? 1 : 0);
}

// HOST: a client's print, judged, filed in the host's save and sent to every client.
void TakePrint(int slot, uint32_t camEid, const std::vector<uint8_t>& pic) {
    if (NowMs() < g_quietUntil) {
        if (g_held.size() < 8) g_held.push_back(Held{slot, camEid, pic});
        UE_LOGI("photo_sync: HOST slot %d's print waits for the host's own print to take its picture", slot);
        return;
    }
    void* const cam = EL::LiveActorOfType(static_cast<EL::ElementId>(camEid), EL::ElementType::Prop);
    coop::RemotePlayer* const rp = coop::players::Registry::Get().Puppet(static_cast<uint8_t>(slot));
    void* const body = rp ? rp->actor() : nullptr;
    ue_wrap::FVector bodyAt{}, at{};
    const bool reach = IsCam(cam) && body && E::TryGetActorLocation(body, bodyAt) &&
                       E::TryGetActorLocation(cam, at) &&
                       (bodyAt.X - at.X) * (bodyAt.X - at.X) + (bodyAt.Y - at.Y) * (bodyAt.Y - at.Y) +
                               (bodyAt.Z - at.Z) * (bodyAt.Z - at.Z) <= kReachCm * kReachCm;
    const uint64_t now = NowMs();
    if (!reach || now - g_lastPrintAt[slot] < kPrintGapMs) {
        UE_LOGW("photo_sync: HOST slot %d's print with camera eid %u refused (%s)", slot, camEid,
                reach ? "a print within a second of its last" : "no camera within 6 m of its body");
        return;
    }
    g_lastPrintAt[slot] = now;
    const int32_t index = PS::AppendGallery(pic);
    if (index < 0 || !PS::WritePhys(index, pic)) {
        UE_LOGW("photo_sync: HOST slot %d's print (%zu bytes) was not filed in the save", slot, pic.size());
        return;
    }
    UE_LOGI("photo_sync: HOST slot %d printed with camera eid %u: photo %d (%zu bytes) filed in the save", slot, camEid,
            index, pic.size());
    g_lastClientPrint = index;
    Publish(index, static_cast<uint8_t>(slot));
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    const bool m = sg::WatchName(kRowMake, kTagMake, &OnMakePre, nullptr);
    const bool p = sg::WatchName(kRowPaper, kTagPaper, &OnPaperPre, nullptr);
    const bool l = sg::WatchName(kRowLoad, kTagLoad, nullptr, &OnLoadPost);
    if (!m || !p || !l) UE_LOGE("photo_sync: a watch was refused (makePhoto=%d checkPaper=%d loadImage=%d)", m, p, l);
}

void Tick() {
    if (IsClient()) RunDue();
    if (IsHost() && !g_held.empty() && NowMs() >= g_quietUntil) {
        std::vector<Held> held;
        held.swap(g_held);
        for (const Held& h : held) TakePrint(h.slot, h.cam, h.pic);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= g_nextSweep) {
        g_nextSweep = now + std::chrono::seconds(1);
        if (!g_asm.Idle()) g_asm.Sweep(now, kTtl);
        for (auto it = g_parts.begin(); it != g_parts.end();)
            it = now - it->second.started > kTtl ? g_parts.erase(it) : std::next(it);
    }
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    const struct { const ue_wrap::facts::Function* row; int tag; } rows[] = {
        {&kRowMake, kTagMake}, {&kRowPaper, kTagPaper}, {&kRowLoad, kTagLoad}};
    for (const auto& r : rows)
        if (!sg::NameWatchSettled(*r.row, r.tag)) return;
    g_settled = true;
    int live = 0;
    for (const auto& r : rows) live += sg::NameWatchLive(*r.row, r.tag) ? 1 : 0;
    if (live == 3) UE_LOGI("photo_sync: the watches on the camera and the photo are live");
    else UE_LOGE("photo_sync: %d of 3 watches on the camera and the photo are live -- a print is its machine's alone", live);
}

void OnChunk(N::Session&, const N::BlobChunkPayload& chunk, int senderPeerSlot) {
    if (senderPeerSlot < 0 || senderPeerSlot >= static_cast<int>(N::kMaxPeers)) return;
    std::vector<uint8_t> blob;
    if (!g_asm.OnChunk(chunk, static_cast<uint8_t>(senderPeerSlot), blob)) return;
    if (blob.size() <= kHead || blob.size() > kHead + kPart) {
        UE_LOGW("photo_sync: a picture's part of %zu bytes from slot %d -- dropping", blob.size(), senderPeerSlot);
        return;
    }
    const uint8_t op = blob[0], grant = blob[1], part = blob[2], parts = blob[3];
    const uint32_t id = Get32(blob, 4), cam = Get32(blob, 8), total = Get32(blob, 12);
    const bool right = (op == kOpPrint && IsHost() && senderPeerSlot > 0) || (op == kOpImage && IsClient());
    if (!right || parts == 0 || part >= parts || total == 0 || total > kMaxTotal ||
        parts != (total + kPart - 1) / kPart) {
        UE_LOGW("photo_sync: a picture's part (op %u, %u of %u, %u bytes) the wrong way or malformed from slot %d -- "
                "dropping", op, part, parts, total, senderPeerSlot);
        return;
    }
    const size_t from = static_cast<size_t>(part) * kPart;
    const size_t n = blob.size() - kHead;
    if (n != (total - from < kPart ? total - from : kPart)) return;
    Parts& p = g_parts[{static_cast<uint8_t>(senderPeerSlot), op, id}];
    if (p.parts == 0) {
        p.grant = grant;
        p.parts = parts;
        p.cam = cam;
        p.total = total;
        p.have.assign(parts, false);
        p.bytes.assign(total, 0);
        p.started = std::chrono::steady_clock::now();
    }
    if (p.parts != parts || p.total != total || p.have[part]) return;
    p.have[part] = true;
    std::memcpy(p.bytes.data() + from, blob.data() + kHead, n);
    if (++p.got < p.parts) return;
    const Parts whole = std::move(p);
    g_parts.erase({static_cast<uint8_t>(senderPeerSlot), op, id});
    if (op == kOpPrint) TakePrint(senderPeerSlot, whole.cam, whole.bytes);
    else TakeImage(whole.grant, static_cast<int32_t>(id), whole.bytes);
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    const int32_t n = PS::Count(true);
    std::vector<uint8_t> pic;
    int sent = 0;
    size_t bytes = 0;
    for (int32_t i = 0; i < n; ++i)
        if (PS::Read(true, i, pic) && SendPicture(kOpImage, kNoGrant, static_cast<uint32_t>(i), 0, pic, slot)) {
            ++sent;
            bytes += pic.size();
        }
    if (sent) UE_LOGI("photo_sync: HOST sent %d printed pictures (%zu bytes) to joining slot %d", sent, bytes, slot);
}

int32_t LastClientPrint() { return g_lastClientPrint; }

void OnPeerGone(uint8_t slot) {
    g_asm.ClearSlot(slot);
    for (auto it = g_parts.begin(); it != g_parts.end();)
        it = std::get<0>(it->first) == slot ? g_parts.erase(it) : std::next(it);
    if (slot < N::kMaxPeers) g_lastPrintAt[slot] = 0;
}

void OnDisconnect() {
    g_asm.Clear();
    g_parts.clear();
    g_due.clear();
    g_held.clear();
    g_quietUntil = 0;
    g_lastClientPrint = -1;
    g_published.clear();
    g_applying = g_noPaper = nullptr;
    for (uint64_t& t : g_lastPrintAt) t = 0;
}

}  // namespace coop::photo_sync
