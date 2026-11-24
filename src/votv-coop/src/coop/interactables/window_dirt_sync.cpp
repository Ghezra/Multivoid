// coop/interactables/window_dirt_sync.cpp -- see coop/interactables/window_dirt_sync.h.
//
// 0.9.0n. d_window_C's begin play sets a 120 s looping timer on `dirtify`, which runs `dirty`: a splotch of
// mat_cleanbrush_glassdirt at a random place, size and angle, refused while a stroke session (`cv`) is open or a
// signal plays. getCode encodes the render target (TextureRenderTarget2DToBytes, format 2, quality 90); the gamemode
// saves it as the save slot's `windowShitBytes` and its load hands it to setCode. The timer's call reaches `dirtify`
// through ProcessEvent, where a watch sees it.

#include "coop/interactables/window_dirt_sync.h"

#include "coop/interactables/window_stroke_sync.h"
#include "coop/net/blob_chunks.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/devices/window_canvas.h"
#include "ue_wrap/facts/function_row.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace coop::window_dirt_sync {
namespace {

namespace N  = coop::net;
namespace sg = ue_wrap::script_gate;
namespace WC = ue_wrap::window_canvas;
namespace BC = coop::blob_chunks;

UE_FACT_FN(kRowDirtify, "window_dirt_sync", L"d_window_C", L"dirtify", Declared);
constexpr int kTag = 0x57445301;  // 'WDS'

constexpr size_t kHead = 12;              // [part][parts][0][0][u32 id][u32 total]
constexpr size_t kPart = 50000;           // a part's picture bytes: one blob
constexpr size_t kMaxTotal = 32 * kPart;  // the window's picture is far under this
static_assert(kHead + kPart <= BC::MaxBlobBytes(), "a picture's part must fit one blob");
constexpr uint64_t kAfterSplotchMs = 300;  // the splotch is drawn by then
constexpr uint64_t kDabsQuietMs = 1500;    // the host sends once the dabs on its window stop this long
constexpr uint64_t kSendGapMs = 1000;
constexpr uint64_t kOwnQuietMs = 600;      // a client applies once its own player's dabs stop this long
const auto kTtl = std::chrono::seconds(20);

N::Session* g_session = nullptr;
bool g_watched = false, g_settled = false;

// HOST
bool g_pending = false;
uint64_t g_splotchAt = 0, g_lastDabAt = 0, g_lastSentAt = 0;
uint64_t g_dabsSeen = 0;
uint32_t g_sentHash = 0, g_nextId = 1, g_blobSeq = 1;
uint32_t g_sends = 0, g_splotches = 0;

// CLIENT
BC::Assembler g_asm;
std::chrono::steady_clock::time_point g_nextSweep{};
struct Parts {
    uint32_t total = 0;
    uint8_t parts = 0;
    std::map<uint8_t, std::vector<uint8_t>> got;
};
std::map<uint32_t, Parts> g_parts;  // by picture id
std::vector<uint8_t> g_held;        // the newest whole picture, not yet drawn
uint64_t g_ownDabs = 0, g_ownDabAt = 0;
uint32_t g_refused = 0, g_shown = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }
uint64_t NowMs() { return ::GetTickCount64(); }

uint32_t Fnv(const std::vector<uint8_t>& b) {
    uint32_t h = 2166136261u;
    for (uint8_t c : b) h = (h ^ c) * 16777619u;
    return h;
}

void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
uint32_t Get32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

bool SendPicture(const std::vector<uint8_t>& pic, int slot) {
    if (pic.empty() || pic.size() > kMaxTotal) return false;
    const size_t parts = (pic.size() + kPart - 1) / kPart;
    const uint32_t id = g_nextId++;
    bool ok = true;
    for (size_t i = 0; i < parts; ++i) {
        const size_t from = i * kPart, n = pic.size() - from < kPart ? pic.size() - from : kPart;
        std::vector<uint8_t> blob(kHead + n, 0);
        blob[0] = static_cast<uint8_t>(i);
        blob[1] = static_cast<uint8_t>(parts);
        Put32(blob, 4, id);
        Put32(blob, 8, static_cast<uint32_t>(pic.size()));
        std::memcpy(blob.data() + kHead, pic.data() + from, n);
        ok = (slot >= 0 ? BC::SendBlobToSlot(g_session, slot, N::ReliableKind::WindowImage, g_blobSeq++, blob)
                        : BC::SendBlob(g_session, N::ReliableKind::WindowImage, g_blobSeq++, blob)) && ok;
    }
    return ok;
}

// CLIENT: the timer's splotch is the host's to draw.
sg::Verdict OnDirtifyPre(const sg::Call&) {
    if (!IsClient()) return sg::Verdict::Run;
    if (++g_refused <= 3 || g_refused % 20 == 0)
        UE_LOGI("window_dirt_sync: CLIENT its window's dirt splotch refused (#%u) -- the host's picture brings dirt",
                g_refused);
    return sg::Verdict::Cancel;
}

// HOST: a splotch drew; the picture goes out once it is in the target.
void OnDirtifyPost(const sg::Call&) {
    if (!IsHost()) return;
    ++g_splotches;
    g_splotchAt = NowMs();
    g_pending = true;
}

bool AnyClientReady() {
    for (int slot = 1; slot < static_cast<int>(N::kMaxPeers); ++slot)
        if (g_session->IsSlotWorldReady(slot)) return true;
    return false;
}

void HostTick(uint64_t now) {
    const uint64_t dabs = coop::window_stroke_sync::DabsDrawn(false);
    if (dabs != g_dabsSeen) {
        g_dabsSeen = dabs;
        g_lastDabAt = now;
        g_pending = true;
    }
    if (!g_pending || now < g_splotchAt + kAfterSplotchMs || now < g_lastDabAt + kDabsQuietMs ||
        now < g_lastSentAt + kSendGapMs)
        return;
    void* const win = coop::window_stroke_sync::Window();
    if (!win || WC::SessionOpen(win) || !AnyClientReady()) return;
    g_lastSentAt = now;
    std::vector<uint8_t> pic;
    if (!WC::ReadImage(win, pic)) {
        UE_LOGW("window_dirt_sync: HOST the window's picture did not read -- trying again in 1 s");
        return;
    }
    g_pending = false;
    const uint32_t h = Fnv(pic);
    if (h == g_sentHash) return;
    const bool ok = SendPicture(pic, -1);
    if (ok) g_sentHash = h;
    ++g_sends;
    UE_LOGI("window_dirt_sync: HOST sent its window's picture (%zu bytes, hash %08x, %u splotches so far, sent=%d)",
            pic.size(), h, g_splotches, ok ? 1 : 0);
}

void ClientTick(uint64_t now) {
    const uint64_t own = coop::window_stroke_sync::DabsDrawn(true);
    if (own != g_ownDabs) {
        g_ownDabs = own;
        g_ownDabAt = now;
    }
    if (g_held.empty() || now < g_ownDabAt + kOwnQuietMs) return;
    void* const win = coop::window_stroke_sync::Window();
    if (!win || WC::SessionOpen(win)) return;
    if (!WC::DrawImage(win, g_held)) return;  // a signal playing or the window not ready: held for the next tick
    ++g_shown;
    UE_LOGI("window_dirt_sync: CLIENT its window shows the host's picture (%zu bytes, hash %08x)", g_held.size(),
            Fnv(g_held));
    g_held.clear();
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    if (!sg::WatchName(kRowDirtify, kTag, &OnDirtifyPre, &OnDirtifyPost))
        UE_LOGE("window_dirt_sync: the watch on the window's dirtify was refused");
}

void Tick() {
    if (g_watched && !g_settled) {
        sg::ResolvePendingNames();
        if (sg::NameWatchSettled(kRowDirtify, kTag)) {
            g_settled = true;
            if (sg::NameWatchLive(kRowDirtify, kTag)) UE_LOGI("window_dirt_sync: the watch on the window's dirtify is live");
            else UE_LOGE("window_dirt_sync: the watch on dirtify is not live -- each peer's window dirties on its own");
        }
    }
    if (!g_session || !g_session->connected()) return;
    const uint64_t now = NowMs();
    if (IsHost()) HostTick(now);
    else if (IsClient()) ClientTick(now);
    const auto t = std::chrono::steady_clock::now();
    if (t >= g_nextSweep) {
        g_nextSweep = t + std::chrono::seconds(1);
        if (!g_asm.Idle()) g_asm.Sweep(t, kTtl);
    }
}

void OnChunk(const N::BlobChunkPayload& chunk, int senderPeerSlot) {
    if (senderPeerSlot != 0 || !IsClient()) return;  // the host's alone
    std::vector<uint8_t> blob;
    if (!g_asm.OnChunk(chunk, 0, blob)) return;
    if (blob.size() <= kHead || blob.size() > kHead + kPart) {
        UE_LOGW("window_dirt_sync: a picture's part of %zu bytes -- dropping", blob.size());
        return;
    }
    const uint8_t part = blob[0], parts = blob[1];
    const uint32_t id = Get32(blob, 4), total = Get32(blob, 8);
    if (parts == 0 || part >= parts || total == 0 || total > kMaxTotal || parts != (total + kPart - 1) / kPart) {
        UE_LOGW("window_dirt_sync: a picture's part (%u of %u, %u bytes) malformed -- dropping", part, parts, total);
        return;
    }
    Parts& p = g_parts[id];
    p.total = total;
    p.parts = parts;
    p.got[part].assign(blob.begin() + kHead, blob.end());
    if (p.got.size() < parts) return;
    std::vector<uint8_t> pic;
    pic.reserve(total);
    for (auto& kv : p.got) pic.insert(pic.end(), kv.second.begin(), kv.second.end());
    // An older picture whose parts are still held is superseded by this one.
    g_parts.erase(g_parts.begin(), g_parts.upper_bound(id));
    if (pic.size() != total) {
        UE_LOGW("window_dirt_sync: a picture of %zu bytes where %u were named -- dropping", pic.size(), total);
        return;
    }
    g_held.swap(pic);
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    void* const win = coop::window_stroke_sync::Window();
    std::vector<uint8_t> pic;
    if (!win || !WC::ReadImage(win, pic)) {
        UE_LOGW("window_dirt_sync: HOST the window's picture did not read for joining slot %d", slot);
        return;
    }
    const bool ok = SendPicture(pic, slot);
    UE_LOGI("window_dirt_sync: HOST sent its window's picture (%zu bytes) to joining slot %d (ok=%d)", pic.size(), slot,
            ok ? 1 : 0);
}

void OnDisconnect() {
    if (g_sends || g_splotches || g_refused || g_shown)
        UE_LOGI("window_dirt_sync: session end -- host splotches %u, pictures sent %u; client splotches refused %u, "
                "pictures shown %u", g_splotches, g_sends, g_refused, g_shown);
    g_pending = false;
    g_splotchAt = g_lastDabAt = g_lastSentAt = g_dabsSeen = g_ownDabs = g_ownDabAt = 0;
    g_sentHash = 0;
    g_sends = g_splotches = g_refused = g_shown = 0;
    g_parts.clear();
    g_held.clear();
    g_asm.Clear();
}

}  // namespace coop::window_dirt_sync
