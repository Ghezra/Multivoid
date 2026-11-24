// coop/props/custom_picture_sync.cpp -- see coop/props/custom_picture_sync.h.
//
// 0.9.0n. prop_picture_h_C (and _s, _t, _v, which inherit it): action 4 with lib's iscustom opens the gamemode's picker
// over the frame's size's pictures; intComs_texturePickerApply sets `filename` and the texture; `upd` loads the custom
// picture of that `filename` (lib's get custom pic) onto `dynmat`. prop_poster_C: its RMB opens the picker over
// `posters`; the apply sets `filename` and runs setTex, which, `custom` set, finds `filename` in filenames_posters. Its
// put spawns a poster_C with its `type`, `filename` and `custom`. poster_C: its press, `custom` set, opens the picker;
// the apply sets `filename` and runs setPoster. prop_rug_c_C (a held custom rug) and rug_C (one laid, rug_c_C the
// custom one): the apply sets `filename` and runs setSkin, which finds it in the gamemode's `rugs`. Each saves
// `filename` in its record's strings.

#include "coop/props/custom_picture_sync.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/build_piece.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace coop::custom_picture_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace sg = ue_wrap::script_gate;
namespace EL = coop::element;
namespace B  = ue_wrap::build_piece;

UE_FACT_FN(kRowFrame, "custom_picture_sync", L"prop_picture_h_C", L"intComs_texturePickerApply", Declared);
UE_FACT_FN(kRowHeld, "custom_picture_sync", L"prop_poster_C", L"intComs_texturePickerApply", Declared);
UE_FACT_FN(kRowWall, "custom_picture_sync", L"poster_C", L"intComs_texturePickerApply", Declared);
UE_FACT_FN(kRowHeldRug, "custom_picture_sync", L"prop_rug_c_C", L"intComs_texturePickerApply", Declared);
UE_FACT_FN(kRowRug, "custom_picture_sync", L"rug_C", L"intComs_texturePickerApply", Declared);
constexpr int kTag = 0x43505301;  // 'CPS'

constexpr float kReachCm = 800.f;
constexpr uint64_t kGapMs = 250;
constexpr int kCap = 256;

// The frames and held posters a joiner is sent, each by its exact class (the world finder takes no subclasses).
constexpr const wchar_t* kPropClasses[] = {
    L"prop_picture_h_C", L"prop_picture_s_C", L"prop_picture_t_C", L"prop_picture_v_C", L"prop_poster_C",
    L"prop_poster_C_C",  L"prop_poster_0_C",  L"prop_poster_1_C",  L"prop_poster_2_C",  L"prop_poster_3_C",
    L"prop_poster_4_C",  L"prop_poster_5_C",  L"prop_poster_6_C",  L"prop_poster_7_C",  L"prop_poster_8_C",
    L"prop_rug_c_C",
};

// A frame; a held poster; a poster on a wall; a held custom rug; a rug laid. A wall poster and a laid rug are build
// pieces (coop/world/build_sync), named on the wire by their save key; the rest by their element id.
enum class Family : uint8_t { None, Frame, Held, Wall, HeldRug, Rug };

bool Keyed(Family f) { return f == Family::Wall || f == Family::Rug; }

N::Session* g_session = nullptr;
bool g_watched = false;
bool g_settled = false;
void* g_applying = nullptr;  // the piece whose name this lane is writing
uint64_t g_lastAt[N::kMaxPeers] = {};
uint32_t g_asked = 0, g_applied = 0, g_refused = 0, g_shown = 0;
void* g_found[kCap];

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }
uint64_t NowMs() { return ::GetTickCount64(); }

Family FamilyOf(void* obj) {
    if (!obj || !R::IsLive(obj)) return Family::None;
    const R::FName& cls = R::NameOf(R::ClassOf(obj));
    if (R::NameStartsWith(cls, L"prop_picture_")) return Family::Frame;
    if (R::NameStartsWith(cls, L"prop_poster_")) return Family::Held;
    if (R::NameStartsWith(cls, L"poster_")) return Family::Wall;
    if (R::NameStartsWith(cls, L"prop_rug_")) return Family::HeldRug;
    if (R::NameStartsWith(cls, L"rug_")) return Family::Rug;
    return Family::None;
}

const wchar_t* PaintFn(Family f) {
    switch (f) {
    case Family::Frame: return L"upd";
    case Family::Held: return L"setTex";
    case Family::Wall: return L"setPoster";
    case Family::HeldRug:
    case Family::Rug: return L"setSkin";
    default: return nullptr;
    }
}

const char* FamilyName(Family f) {
    switch (f) {
    case Family::Frame: return "frame";
    case Family::Held: return "held poster";
    case Family::Wall: return "wall poster";
    case Family::HeldRug: return "held rug";
    case Family::Rug: return "laid rug";
    default: return "?";
    }
}

R::InstanceOffset s_filename(L"filename");

R::FName* NameField(void* obj) {
    const int32_t off = obj ? s_filename.Of(obj) : -1;
    return off >= 0 ? reinterpret_cast<R::FName*>(static_cast<char*>(obj) + off) : nullptr;
}

bool ReadName(void* obj, std::wstring& out) {
    const R::FName* const n = NameField(obj);
    if (!n) return false;
    out = R::ToString(*n);
    if (out == L"None") out.clear();
    return true;
}

// The piece's name written and painted with its own function, this lane's hand on it.
bool Apply(void* obj, Family f, const std::wstring& name) {
    R::FName* const n = NameField(obj);
    const wchar_t* const fn = PaintFn(f);
    if (!n || !fn) return false;
    *n = ue_wrap::fname_utils::StringToFName(name.empty() ? std::wstring(L"None") : name);
    ue_wrap::ParamFrame frame(R::FindDispatchFunctionCached(R::ClassOf(obj), fn));
    g_applying = obj;
    const bool ran = frame.valid() && ue_wrap::Call(obj, frame);
    g_applying = nullptr;
    return ran;
}

uint32_t EidOf(void* obj) {
    const EL::ElementId eid = EL::Registry::Get().EidForActor(obj);
    return eid == EL::kInvalidId ? 0u : static_cast<uint32_t>(eid);
}

void* PieceByKey(const std::wstring& key) {
    std::vector<void*> pieces;
    B::List(pieces);
    for (void* p : pieces)
        if (Keyed(FamilyOf(p)) && B::KeyOf(p) == key) return p;
    return nullptr;
}

// The piece's wire name: a prop's element id, a build piece's save key. False when the host could not name it.
bool Identify(void* obj, Family f, N::CustomPicturePayload& p) {
    if (Keyed(f)) {
        const std::wstring key = B::KeyOf(obj);
        if (key.empty() || key.size() > sizeof(p.key.data)) return false;
        N::WireKeyFromString(key, p.key);
        return true;
    }
    p.eid = EidOf(obj);
    return p.eid != 0;
}

void* Resolve(const N::CustomPicturePayload& p) {
    void* const obj = p.key.len ? PieceByKey(N::StringFromWireKey(p.key))
                                : EL::LiveActorOfType(static_cast<EL::ElementId>(p.eid), EL::ElementType::Prop);
    return FamilyOf(obj) == Family::None ? nullptr : obj;
}

bool PutName(N::CustomPicturePayload& p, const std::wstring& name) {
    if (name.size() > sizeof(p.name) / sizeof(p.name[0])) return false;
    p.len = static_cast<uint8_t>(name.size());
    for (size_t i = 0; i < name.size(); ++i) p.name[i] = static_cast<uint16_t>(name[i]);
    return true;
}

std::wstring TakeName(const N::CustomPicturePayload& p) {
    std::wstring s;
    for (uint8_t i = 0; i < p.len; ++i) s.push_back(static_cast<wchar_t>(p.name[i]));
    return s;
}

std::wstring Describe(const N::CustomPicturePayload& p) {
    return p.key.len ? L"key '" + N::StringFromWireKey(p.key) + L"'" : L"eid " + std::to_wstring(p.eid);
}

// HOST: the piece's name to one slot, or every client at -1.
bool SendState(void* obj, Family f, int slot) {
    N::CustomPicturePayload p{};
    std::wstring name;
    if (!Identify(obj, f, p) || !ReadName(obj, name) || !PutName(p, name)) return false;
    return slot >= 0 ? g_session->SendReliableToSlot(static_cast<uint8_t>(slot), N::ReliableKind::PictureState, &p,
                                                     sizeof(p))
                     : g_session->SendReliable(N::ReliableKind::PictureState, &p, sizeof(p));
}

// A pick on this machine: a client's goes to the host, the host's to every client.
void OnApplyPost(const sg::Call& c) {
    void* const obj = c.object;
    const Family f = FamilyOf(obj);
    if (f == Family::None || obj == g_applying) return;
    if (IsHost()) {
        const bool ok = SendState(obj, f, -1);
        std::wstring name;
        ReadName(obj, name);
        UE_LOGI("custom_picture_sync: HOST picked '%ls' for its %s (sent=%d)", name.c_str(), FamilyName(f), ok ? 1 : 0);
        return;
    }
    if (!IsClient()) return;
    N::CustomPicturePayload p{};
    std::wstring name;
    if (!Identify(obj, f, p) || !ReadName(obj, name) || !PutName(p, name)) {
        UE_LOGW("custom_picture_sync: CLIENT a pick on a %s the host does not name, or a name over 94 -- kept here",
                FamilyName(f));
        return;
    }
    const bool ok = g_session->SendReliable(N::ReliableKind::PictureAsk, &p, sizeof(p));
    ++g_asked;
    UE_LOGI("custom_picture_sync: CLIENT picked '%ls' for %s %ls -- asked of the host (ok=%d)", name.c_str(),
            FamilyName(f), Describe(p).c_str(), ok ? 1 : 0);
}

bool InReach(int slot, void* obj) {
    coop::RemotePlayer* const rp = coop::players::Registry::Get().Puppet(static_cast<uint8_t>(slot));
    void* const body = rp ? rp->actor() : nullptr;
    ue_wrap::FVector a{}, b{};
    if (!body || !E::TryGetActorLocation(body, a) || !E::TryGetActorLocation(obj, b)) return false;
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return dx * dx + dy * dy + dz * dz <= kReachCm * kReachCm;
}

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    for (const auto* row : {&kRowFrame, &kRowHeld, &kRowWall, &kRowHeldRug, &kRowRug})
        if (!sg::WatchName(*row, kTag, nullptr, &OnApplyPost))
            UE_LOGE("custom_picture_sync: a watch on a picker's apply was refused");
}

void Tick() {
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    for (const auto* row : {&kRowFrame, &kRowHeld, &kRowWall, &kRowHeldRug, &kRowRug})
        if (!sg::NameWatchSettled(*row, kTag)) return;
    g_settled = true;
    int live = 0;
    for (const auto* row : {&kRowFrame, &kRowHeld, &kRowWall, &kRowHeldRug, &kRowRug})
        live += sg::NameWatchLive(*row, kTag) ? 1 : 0;
    if (live == 5) UE_LOGI("custom_picture_sync: the watches on the pickers' apply are live");
    else UE_LOGE("custom_picture_sync: %d of 5 picker watches live -- a pick on the others stays on its picker's machine",
                 live);
}

void OnAsk(int senderSlot, const N::CustomPicturePayload& p) {
    if (!IsHost() || senderSlot <= 0 || senderSlot >= static_cast<int>(N::kMaxPeers)) return;
    const uint64_t now = NowMs();
    if (now - g_lastAt[senderSlot] < kGapMs) return;  // a flood is dropped; the next pick sends the name again
    g_lastAt[senderSlot] = now;
    void* const obj = Resolve(p);
    const Family f = FamilyOf(obj);
    const std::wstring name = TakeName(p);
    if (!obj || !InReach(senderSlot, obj)) {
        ++g_refused;
        UE_LOGW("custom_picture_sync: HOST slot %d's pick '%ls' for %ls refused (found %d)", senderSlot, name.c_str(),
                Describe(p).c_str(), obj ? 1 : 0);
        if (obj) SendState(obj, f, senderSlot);  // the host's name takes back what the client showed
        return;
    }
    const bool ran = Apply(obj, f, name);
    ++g_applied;
    const bool sent = SendState(obj, f, -1);
    UE_LOGI("custom_picture_sync: HOST slot %d picked '%ls' for %s %ls (painted=%d, sent=%d)", senderSlot, name.c_str(),
            FamilyName(f), Describe(p).c_str(), ran ? 1 : 0, sent ? 1 : 0);
}

void OnState(const N::CustomPicturePayload& p) {
    if (!IsClient()) return;
    void* const obj = Resolve(p);
    const Family f = FamilyOf(obj);
    const std::wstring name = TakeName(p);
    std::wstring have;
    if (!obj || !ReadName(obj, have)) {
        UE_LOGI("custom_picture_sync: CLIENT '%ls' for %ls: not here to show", name.c_str(), Describe(p).c_str());
        return;
    }
    if (have == name) return;
    const bool ran = Apply(obj, f, name);
    ++g_shown;
    UE_LOGI("custom_picture_sync: CLIENT %s %ls shows '%ls' (was '%ls', painted=%d)", FamilyName(f),
            Describe(p).c_str(), name.c_str(), have.c_str(), ran ? 1 : 0);
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    int sent = 0;
    for (const wchar_t* cls : kPropClasses) {
        const int n = ue_wrap::world_instances::Find(cls, g_found, kCap);
        for (int i = 0; i < n; ++i) {
            std::wstring name;
            if (ReadName(g_found[i], name) && !name.empty() && SendState(g_found[i], FamilyOf(g_found[i]), slot)) ++sent;
        }
    }
    if (sent) UE_LOGI("custom_picture_sync: HOST sent %d named frames and posters to joining slot %d", sent, slot);
}

void OnDisconnect() {
    if (g_asked || g_applied || g_refused || g_shown)
        UE_LOGI("custom_picture_sync: session end -- client asks %u, shown %u; host picks taken %u, refused %u", g_asked,
                g_shown, g_applied, g_refused);
    g_asked = g_applied = g_refused = g_shown = 0;
    g_applying = nullptr;
    for (uint64_t& t : g_lastAt) t = 0;
}

}  // namespace coop::custom_picture_sync
