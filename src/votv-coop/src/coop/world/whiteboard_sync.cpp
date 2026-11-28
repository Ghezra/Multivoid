// coop/world/whiteboard_sync.cpp -- see coop/world/whiteboard_sync.h.

#include "coop/world/whiteboard_sync.h"

#include "coop/net/blob_chunks.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/whiteboard.h"

#include <windows.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace coop::whiteboard_sync {
namespace {

namespace N  = coop::net;
namespace W  = ue_wrap::whiteboard;
namespace BC = coop::blob_chunks;
namespace sg = ue_wrap::script_gate;

UE_FACT_FN(kRowRelease, "whiteboard_sync", L"prop_whiteboard_C", L"Release", Declared);
constexpr int    kTagRelease = 0x57425031;  // 'WBP1'
constexpr size_t kMaxBoards = 16;
constexpr size_t kBlobHead = 12;            // [u64 key][u32 picture length]
constexpr uint64_t kSendGapMs = 200;        // one board's pictures, at most four a second
constexpr uint64_t kRescanMs = 30000;   // a full object-array walk: rare, and the list survives it
constexpr uint64_t kSafetyMs = 1000;        // idle: one read a second, which is the visible latency of a stroke
constexpr uint64_t kActiveMs = 400;         // a stroke runs: two and a half reads a second

struct Board {
    void*    prop = nullptr;
    void*    surface = nullptr;
    uint64_t key = 0;
    uint64_t sentAt = 0;
    uint32_t lastHash = 0;   // of the picture this peer holds, so a repeat is not sent
    bool     dirtyLocal = false;  // this peer's own stroke changed the board: only that may send it
    uint32_t lastSentHash = 0;     // of the picture this peer last sent
    size_t   lastSize = 0;         // of the picture this peer last sent or took: the stable signal, since a re-encode of the same board yields the same length
    uint64_t appliedAt = 0;  // when the wire last put a picture here, so a poll does not echo it back
    bool     said = false;   // the unreadable-board warning was given once
};

std::atomic<N::Session*> g_session{nullptr};
Board    g_boards[kMaxBoards];
size_t   g_boardCount = 0;
uint64_t g_rescanAt = 0;
uint64_t g_safetyAt = 0;
uint32_t g_blobSeq = 1;
uint64_t g_counts[static_cast<size_t>(Count::kCount)] = {};
BC::Assembler g_asm;
// The last picture length per board key: kept outside the board list, which a rescan rebuilds, so a scan can never make
// a board look changed and start an echo.
std::map<uint64_t, size_t> g_lastSize;
bool g_watchLive = false;

uint64_t NowMs() { return ::GetTickCount64(); }
bool IsHost() {
    N::Session* const s = g_session.load(std::memory_order_acquire);
    return s && s->running() && s->role() == N::Role::Host;
}
bool IsClient() {
    N::Session* const s = g_session.load(std::memory_order_acquire);
    return s && s->running() && s->role() == N::Role::Client;
}
void Note(Count c) { ++g_counts[static_cast<size_t>(c)]; }

uint32_t Hash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

Board* FindByProp(void* prop) {
    for (size_t i = 0; i < g_boardCount; ++i)
        if (g_boards[i].prop == prop) return &g_boards[i];
    return nullptr;
}
Board* FindByKey(uint64_t key) {
    for (size_t i = 0; i < g_boardCount; ++i)
        if (g_boards[i].key == key) return &g_boards[i];
    return nullptr;
}

void Rescan(uint64_t now) {
    if (now < g_rescanAt && g_boardCount) return;
    g_rescanAt = now + kRescanMs;
    std::vector<void*> props;
    W::FindBoards(props, kMaxBoards);
    Board kept[kMaxBoards];
    size_t n = 0;
    for (void* prop : props) {
        void* const surface = W::SurfaceOf(prop);
        if (!surface) continue;
        Board b;
        b.prop = prop;
        b.surface = surface;
        b.key = W::KeyOf(prop);
        if (Board* const old = FindByProp(prop)) { b.sentAt = old->sentAt; b.lastHash = old->lastHash; }
        kept[n++] = b;
    }
    std::memcpy(g_boards, kept, n * sizeof(Board));
    g_boardCount = n;
    static size_t said = static_cast<size_t>(-1);
    if (n != said) { said = n; UE_LOGI("whiteboard_sync: %zu board(s) in the world", n); }
}

// The picture of one board as the wire carries it: [u64 key][u32 length][bytes].
bool Encode(Board& b, std::vector<uint8_t>& blob) {
    const std::string picture = W::PictureOf(b.surface);
    if (picture.empty()) return false;
    blob.assign(kBlobHead + picture.size(), 0);
    std::memcpy(blob.data(), &b.key, 8);
    const uint32_t len = static_cast<uint32_t>(picture.size());
    std::memcpy(blob.data() + 8, &len, 4);
    std::memcpy(blob.data() + kBlobHead, picture.data(), picture.size());
    b.lastHash = Hash(picture);
    b.lastSentHash = b.lastHash;
    g_lastSize[b.key] = picture.size();
    return true;
}

bool SendBoard(Board& b, int onlySlot, int skipSlot) {
    std::vector<uint8_t> blob;
    if (!Encode(b, blob)) return false;
    if (!BC::FitsWire(blob)) {
        if (b.sentAt == 0)
            UE_LOGW("whiteboard_sync: the board's picture is %zu bytes and does not fit the wire -- not sent",
                    blob.size() - kBlobHead);
        Note(Count::Refused);
        return false;
    }
    N::Session* const s = g_session.load(std::memory_order_acquire);
    if (!s) return false;
    const bool ok = onlySlot >= 0
                        ? BC::SendBlobToSlot(s, onlySlot, N::ReliableKind::BoardImage, g_blobSeq++, blob)
                        : BC::SendBlob(s, N::ReliableKind::BoardImage, g_blobSeq++, blob);
    if (!ok) return false;
    b.sentAt = NowMs();
    Note(Count::Sent);
    (void)skipSlot;   // the sender's own board is already right; a relay excludes it by slot at the send site
    return true;
}

// A picture this peer's own board gained: the peer that drew it tells the host, and the host tells the world.
void OnBoardChanged(Board& b, const char* how) {
    if (b.sentAt && NowMs() < b.sentAt + kSendGapMs) return;   // coalesce a burst of strokes
    if (!W::CanRead(b.surface)) {
        if (!b.said) { b.said = true; UE_LOGW("whiteboard_sync: board %016llx has no render target to read -- not synced", static_cast<unsigned long long>(b.key)); }
        return;
    }
    std::vector<uint8_t> blob;
    if (!Encode(b, blob)) { if (!b.said) { b.said = true; UE_LOGW("whiteboard_sync: board %016llx's picture did not read", static_cast<unsigned long long>(b.key)); } return; }
    const size_t bytes = blob.size() - kBlobHead;
    if (!BC::FitsWire(blob)) {
        UE_LOGW("whiteboard_sync: board %016llx's picture is %zu bytes and does not fit the wire -- not sent", static_cast<unsigned long long>(b.key), bytes);
        Note(Count::Refused);
        return;
    }
    b.sentAt = NowMs();
    if (IsClient()) {
        N::Session* const s = g_session.load(std::memory_order_acquire);
        if (!s) return;
        b.sentAt = 0;
        if (BC::SendBlobToSlot(s, 0, N::ReliableKind::BoardImage, g_blobSeq++, blob)) { b.sentAt = NowMs(); b.dirtyLocal = false; Note(Count::Sent); UE_LOGI("whiteboard_sync: board %016llx changed (%s, %zu bytes) -- sent to the host", static_cast<unsigned long long>(b.key), how, bytes); }
        else UE_LOGW("whiteboard_sync: board %016llx's picture (%zu bytes) did not send to the host", static_cast<unsigned long long>(b.key), bytes);
        return;
    }
    if (IsHost()) {
        N::Session* const s = g_session.load(std::memory_order_acquire);
        if (!s) return;
        b.sentAt = 0;
        if (BC::SendBlob(s, N::ReliableKind::BoardImage, g_blobSeq++, blob)) { b.sentAt = NowMs(); b.dirtyLocal = false; Note(Count::Sent); UE_LOGI("whiteboard_sync: board %016llx changed (%s, %zu bytes) -- sent to every peer", static_cast<unsigned long long>(b.key), how, bytes); }
        else UE_LOGW("whiteboard_sync: board %016llx's picture (%zu bytes) did not send", static_cast<unsigned long long>(b.key), bytes);
    }
}

// HOST: a peer's picture landed here; every other peer gets the same bytes, never a re-encode of this peer's board (a
// re-read lags the strokes that arrived meanwhile, and applying it erases them).
void RelayToAll(const std::vector<uint8_t>& blob, int senderSlot) {
    N::Session* const s = g_session.load(std::memory_order_acquire);
    if (!s) return;
    for (int slot = 1; slot < static_cast<int>(N::kMaxPeers); ++slot) {
        if (slot == senderSlot || !s->IsSlotWorldReady(slot)) continue;
        if (BC::SendBlobToSlot(s, slot, N::ReliableKind::BoardImage, g_blobSeq++, blob)) Note(Count::Sent);
    }
}

// A stroke ended on `prop`: this peer's board changed.
void OnReleasePost(const sg::Call& c) {
    if (!c.object) return;
    Board* const b = FindByProp(c.object);
    if (!b) return;
    b->dirtyLocal = true;
    OnBoardChanged(*b, "stroke released");
}
}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    g_boardCount = 0;
    g_rescanAt = 0;
    g_asm.Clear();
}

void Tick() {
    N::Session* const s = g_session.load(std::memory_order_acquire);
    if (!s || !s->running()) return;
    if (!g_watchLive) {
        g_watchLive = sg::WatchName(kRowRelease, kTagRelease, nullptr, &OnReleasePost);
        if (g_watchLive) UE_LOGI("whiteboard_sync: the watch on prop_whiteboard_C.Release is live");
    }
    const uint64_t now = NowMs();
    Rescan(now);
    if (!g_boardCount) return;
    // The detector: a board's picture is read every half second on EVERY peer, and a picture that moved is sent. A
    // stroke can end without announcing anything (a full clear, a mirror whose Release this world never calls), so the
    // poll is the lane's floor; the Release watch above only makes a stroke's last picture go out sooner.
    if (now < g_safetyAt) return;
    bool active = false;
    for (size_t i = 0; i < g_boardCount; ++i) if (W::IsActive(g_boards[i].surface)) active = true;
    g_safetyAt = now + (active ? kActiveMs : kSafetyMs);
    for (size_t i = 0; i < g_boardCount; ++i) {
        Board& b = g_boards[i];
        // The picture's LENGTH is the change signal: an identical board re-encodes to a different string but the same
        // length, while a stroke, an eraser pass or a clear move it. Applying a picture from the wire refreshes this, so
        // a board the wire wrote is never sent back.
        if (!W::CanRead(b.surface)) { OnBoardChanged(b, "unreadable"); continue; }
        const std::string picture = W::PictureOf(b.surface);
        if (picture.empty()) {
            if (!b.said) { b.said = true; UE_LOGW("whiteboard_sync: board %016llx's picture did not read (rt=%s) -- not synced", static_cast<unsigned long long>(b.key), W::CanRead(b.surface) ? "yes" : "no"); }
            continue;
        }
        if (!b.said) { b.said = true; UE_LOGI("whiteboard_sync: board %016llx reads %zu bytes of picture", static_cast<unsigned long long>(b.key), picture.size()); }
        if (now < b.appliedAt + 2000) {   // the wire just wrote this board: absorb this peer's own re-encode
            g_lastSize[b.key] = picture.size();
            continue;
        }
        const size_t last = g_lastSize.count(b.key) ? g_lastSize[b.key] : 0;
        if (picture.size() == last) continue;
        OnBoardChanged(b, "poll");
    }
}

void OnChunk(const N::BlobChunkPayload& chunk, int senderSlot) {
    if (chunk.chunkIdx == 0) UE_LOGI("whiteboard_sync: a picture arrives in %u part(s) from slot %d", chunk.chunks, senderSlot);;
    // Own reassembly, one entry per (sender, seq): a part index is remembered, so a retransmitted or reordered part cannot
    // upset the count, and the blob is joined in index order.
    struct Assembly {
        std::vector<std::vector<uint8_t>> parts;
        size_t got = 0;
        uint64_t at = 0;
    };
    static std::map<std::pair<int, uint32_t>, Assembly> asm_;
    auto& a = asm_[{senderSlot, chunk.blobSeq}];
    if (a.parts.size() != chunk.chunks) {
        a.parts.assign(chunk.chunks, {});
        a.got = 0;
    }
    const uint16_t lenField = static_cast<uint16_t>(chunk.chunkLen & 0x7FFF);
    if (chunk.chunkIdx >= a.parts.size() || lenField > sizeof(chunk.data)) return;
    if (a.parts[chunk.chunkIdx].empty()) {
        a.parts[chunk.chunkIdx].assign(chunk.data, chunk.data + lenField);
        ++a.got;
    }
    a.at = NowMs();
    if (a.got != a.parts.size()) {
        if (a.got == 0) asm_.erase({senderSlot, chunk.blobSeq});
        return;
    }
    std::vector<uint8_t> blob;
    for (const auto& part : a.parts) blob.insert(blob.end(), part.begin(), part.end());
    asm_.erase({senderSlot, chunk.blobSeq});    if (blob.size() <= kBlobHead) return;
    uint64_t key = 0;
    uint32_t len = 0;
    std::memcpy(&key, blob.data(), 8);
    std::memcpy(&len, blob.data() + 8, 4);
    if (len == 0 || len != blob.size() - kBlobHead) {
        UE_LOGW("whiteboard_sync: slot %d's picture header says %u bytes over %zu -- dropped", senderSlot, len,
                blob.size() - kBlobHead);
        return;
    }
    const std::string picture(reinterpret_cast<const char*>(blob.data() + kBlobHead), len);
    Board* b = FindByKey(key);
    if (!b) { Rescan(NowMs()); b = FindByKey(key); }   // a board found after this peer's last scan
    if (!b) {
        UE_LOGW("whiteboard_sync: slot %d's picture names board %016llx, which this world does not hold -- dropped",
                senderSlot, static_cast<unsigned long long>(key));
        return;
    }
    if (!W::ApplyPicture(b->surface, picture)) {
        Note(Count::Refused);
        UE_LOGW("whiteboard_sync: slot %d's picture did not apply here", senderSlot);
        return;
    }
    // What this peer now holds is what its own surface reads, not the bytes that came in: a board whose encoding differs
    // on every read (a re-encode, a redraw) would otherwise look changed here and be sent straight back.
    const std::string now = W::PictureOf(b->surface);
    b->lastHash = now.empty() ? Hash(picture) : Hash(now);
    b->dirtyLocal = false;
    g_lastSize[b->key] = len;   // the length that came over the wire, whatever this peer's re-encode says
    b->appliedAt = NowMs();
    b->appliedAt = NowMs();
    b->sentAt = NowMs();
    Note(Count::Applied);
    UE_LOGI("whiteboard_sync: board %016llx took slot %d's picture (%u bytes)", static_cast<unsigned long long>(key),
            senderSlot, len);
    if (IsHost()) RelayToAll(blob, senderSlot);
}

void OnPeerWorldReady(int slot) {
    N::Session* const s = g_session.load(std::memory_order_acquire);
    if (!s || s->role() != N::Role::Host || slot <= 0) return;
    Rescan(NowMs());
    for (size_t i = 0; i < g_boardCount; ++i)
        if (W::CanRead(g_boards[i].surface) && SendBoard(g_boards[i], slot, -1)) ++g_counts[static_cast<size_t>(Count::JoinSent)];
    UE_LOGI("whiteboard_sync: slot %d's world is up -- %zu board picture(s) sent", slot, g_boardCount);
}

void OnDisconnect() {
    g_watchLive = false;
    g_boardCount = 0;
    g_asm.Clear();
    if (g_counts[static_cast<size_t>(Count::Sent)] || g_counts[static_cast<size_t>(Count::Applied)])
        UE_LOGI("whiteboard_sync: session end -- sent %llu, applied %llu, refused %llu, join sent %llu",
                static_cast<unsigned long long>(g_counts[static_cast<size_t>(Count::Sent)]),
                static_cast<unsigned long long>(g_counts[static_cast<size_t>(Count::Applied)]),
                static_cast<unsigned long long>(g_counts[static_cast<size_t>(Count::Refused)]),
                static_cast<unsigned long long>(g_counts[static_cast<size_t>(Count::JoinSent)]));
    for (uint64_t& v : g_counts) v = 0;
    g_blobSeq = 1;
}

}  // namespace coop::whiteboard_sync
