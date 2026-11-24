// coop/interactables/gasoline_sync.cpp -- see coop/interactables/gasoline_sync.h.
//
// 0.9.0n. cremator_C: actionOptionIndex action 4 with lookingAt_valve flips `pouring` and calls updPour; a timer calls
// pouringTimer, which while `pouring` calls pourOut: a sphere trace from `nozzlePour`, and on a prop_gascan_C aligned
// with `gascanAlign` its `amount` += min(gasAmount, pourRate) (clamped to its max), then gasAmount -= pourRate (not below
// 0) and updPour. The burn tick adds each burned actor's `giveFuel` to gasAmount. prop_gascan_C keeps its fuel in
// `amount`; its gascanFuel and use verbs spend it into a generator, a woodchipper or a lamp on the pouring peer.

#include "coop/interactables/gasoline_sync.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/interactables/interactable_sync.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/devices/cremator.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace coop::gasoline_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace C  = ue_wrap::cremator;
namespace sg = ue_wrap::script_gate;
namespace EL = coop::element;

UE_FACT_FN(kRowPourOut, "gasoline_sync", L"cremator_C", L"pourOut", Declared);
constexpr int kTagPourOut = 0x47415301;  // 'GAS'

constexpr const wchar_t* kCanClass = L"prop_gascan_C";
constexpr const wchar_t* kCrematorClass = L"cremator_C";
constexpr uint64_t kEveryMs = 200;
constexpr float kCanStep = 0.25f;     // a can's fuel is sent when it moved this much
constexpr float kGasStep = 0.5f;      // a cremator's
constexpr float kMaxFuel = 100000.f;  // a value past this is no can's
constexpr int kCap = 128;

N::Session* g_session = nullptr;
bool g_watched = false;
bool g_settled = false;
uint64_t g_nextMs = 0;
void* g_found[kCap];

// HOST: the values last sent.
struct Sent {
    float gas = -1.f;
    bool pouring = false;
    bool mark = false;
};
std::unordered_map<std::wstring, Sent> g_sentCremator;
std::unordered_map<uint32_t, float> g_sentCan;

// CLIENT: the host's last value per can, and the copies written with it at least once (a copy never written holds its
// save's value, which is no spend of this player's).
std::unordered_map<uint32_t, float> g_hostCan;
std::unordered_set<uint32_t> g_written;

uint32_t g_spends = 0, g_canSends = 0, g_fuelSends = 0, g_valvesShown = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }
uint64_t NowMs() { return ::GetTickCount64(); }

uint32_t EidOf(void* obj) {
    const EL::ElementId eid = EL::Registry::Get().EidForActor(obj);
    return eid == EL::kInvalidId ? 0u : static_cast<uint32_t>(eid);
}

R::InstanceOffset s_amount(L"amount");
float* AmountOf(void* can) {
    const int32_t off = can && R::IsLive(can) ? s_amount.Of(can) : -1;
    return off >= 0 ? reinterpret_cast<float*>(static_cast<char*>(can) + off) : nullptr;
}

bool Send(N::ReliableKind kind, const void* p, size_t n, int slot) {
    if (!g_session || !g_session->connected()) return false;
    return slot >= 0 ? g_session->SendReliableToSlot(static_cast<uint8_t>(slot), kind, p, n)
                     : g_session->SendReliable(kind, p, n);
}

bool SendCremator(void* c, const std::wstring& key, float gas, bool pouring, int slot) {
    N::CrematorFuelPayload p{};
    N::WireKeyFromString(key, p.key);
    p.gas = gas;
    p.pouring = pouring ? 1 : 0;
    ++g_fuelSends;
    return Send(N::ReliableKind::CrematorFuel, &p, sizeof(p), slot);
}

bool SendCan(uint32_t eid, float amount, int slot) {
    N::GasCanPayload p{};
    p.eid = eid;
    p.amount = amount;
    ++g_canSends;
    return Send(N::ReliableKind::GasCanState, &p, sizeof(p), slot);
}

// HOST: every cremator's and can's change since the last send.
void HostScan() {
    int n = ue_wrap::world_instances::Find(kCrematorClass, g_found, kCap);
    for (int i = 0; i < n; ++i) {
        void* const c = g_found[i];
        const std::wstring key = coop::interactable_sync::CrematorKey(c);
        float gas = 0.f;
        bool pouring = false;
        if (key.empty() || !C::ReadGasAmount(c, gas) || !C::ReadPouring(c, pouring)) continue;
        Sent& s = g_sentCremator[key];
        if (!s.mark && s.pouring == pouring && std::fabs(s.gas - gas) < kGasStep && !(gas <= 0.f && s.gas > 0.f)) continue;
        if (s.pouring != pouring)
            UE_LOGI("gasoline_sync: HOST cremator '%ls' valve %s, fuel %.1f", key.c_str(), pouring ? "open" : "shut", gas);
        s = Sent{gas, pouring, false};
        SendCremator(c, key, gas, pouring, -1);
    }
    n = ue_wrap::world_instances::Find(kCanClass, g_found, kCap);
    for (int i = 0; i < n; ++i) {
        const uint32_t eid = EidOf(g_found[i]);
        const float* const a = AmountOf(g_found[i]);
        if (!eid || !a) continue;
        const auto it = g_sentCan.find(eid);
        if (it != g_sentCan.end() && std::fabs(it->second - *a) < kCanStep && !(*a <= 0.f && it->second > 0.f)) continue;
        g_sentCan[eid] = *a;
        SendCan(eid, *a, -1);
    }
}

// CLIENT: its copies against the host's values: a fall is this player's spend, a rise is put back.
void ClientScan() {
    const int n = ue_wrap::world_instances::Find(kCanClass, g_found, kCap);
    for (int i = 0; i < n; ++i) {
        const uint32_t eid = EidOf(g_found[i]);
        float* const a = AmountOf(g_found[i]);
        if (!eid || !a) continue;
        const auto it = g_hostCan.find(eid);
        if (it == g_hostCan.end()) continue;
        if (!g_written.count(eid) || *a > it->second + kCanStep) {
            *a = it->second;
            g_written.insert(eid);
            continue;
        }
        if (*a < it->second - kCanStep) {
            N::GasCanPayload p{};
            p.eid = eid;
            p.amount = *a < 0.f ? 0.f : *a;
            if (Send(N::ReliableKind::GasCanSpend, &p, sizeof(p), -1)) {
                ++g_spends;
                UE_LOGI("gasoline_sync: CLIENT can eid %u spent %.1f here (%.1f left) -- told the host", eid,
                        it->second - p.amount, p.amount);
                it->second = p.amount;
            }
        }
    }
}

// CLIENT: its copy of a cremator pours nothing; the host's pour fills the host's can.
sg::Verdict OnPourOutPre(const sg::Call&) { return IsClient() ? sg::Verdict::Cancel : sg::Verdict::Run; }

}  // namespace

void Install(N::Session* session) {
    g_session = session;
    if (g_watched) return;
    g_watched = true;
    if (!sg::WatchName(kRowPourOut, kTagPourOut, &OnPourOutPre, nullptr))
        UE_LOGE("gasoline_sync: the watch on the cremator's pourOut was refused");
}

void Tick() {
    const uint64_t now = NowMs();
    if (now >= g_nextMs && g_session && g_session->connected()) {
        g_nextMs = now + kEveryMs;
        if (IsHost()) HostScan();
        else if (IsClient()) ClientScan();
    }
    if (!g_watched || g_settled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowPourOut, kTagPourOut)) return;
    g_settled = true;
    if (sg::NameWatchLive(kRowPourOut, kTagPourOut)) UE_LOGI("gasoline_sync: the watch on the cremator's pourOut is live");
    else UE_LOGE("gasoline_sync: the watch on the cremator's pourOut is not live -- a client's copy pours its own fuel");
}

void MarkCremator(void* cremator) {
    const std::wstring key = coop::interactable_sync::CrematorKey(cremator);
    if (!key.empty()) g_sentCremator[key].mark = true;
}

void SendJoinSnapshotForSlot(int slot) {
    if (!IsHost() || slot <= 0) return;
    int sent = 0;
    int n = ue_wrap::world_instances::Find(kCrematorClass, g_found, kCap);
    for (int i = 0; i < n; ++i) {
        const std::wstring key = coop::interactable_sync::CrematorKey(g_found[i]);
        float gas = 0.f;
        bool pouring = false;
        if (!key.empty() && C::ReadGasAmount(g_found[i], gas) && C::ReadPouring(g_found[i], pouring) &&
            SendCremator(g_found[i], key, gas, pouring, slot))
            ++sent;
    }
    n = ue_wrap::world_instances::Find(kCanClass, g_found, kCap);
    for (int i = 0; i < n; ++i) {
        const uint32_t eid = EidOf(g_found[i]);
        const float* const a = AmountOf(g_found[i]);
        if (eid && a && SendCan(eid, *a, slot)) ++sent;
    }
    if (sent) UE_LOGI("gasoline_sync: HOST sent %d cremators and cans to joining slot %d", sent, slot);
}

void OnCrematorFuel(const N::CrematorFuelPayload& p) {
    if (!IsClient()) return;
    void* const c = coop::interactable_sync::ResolveCremator(N::StringFromWireKey(p.key));
    bool pouring = false;
    if (!c || !C::ReadPouring(c, pouring)) return;
    C::WriteGasAmount(c, p.gas);
    if (pouring != (p.pouring != 0)) {
        C::WritePouring(c, p.pouring != 0);
        const bool ran = C::CallUpdPour(c);
        ++g_valvesShown;
        UE_LOGI("gasoline_sync: CLIENT cremator valve %s, fuel %.1f, as the host's (updPour ran=%d)",
                p.pouring ? "open" : "shut", p.gas, ran ? 1 : 0);
    }
}

void OnCanState(const N::GasCanPayload& p) {
    if (!IsClient()) return;
    g_hostCan[p.eid] = p.amount;
    void* const can = EL::LiveActorOfType(static_cast<EL::ElementId>(p.eid), EL::ElementType::Prop);
    if (float* const a = AmountOf(can)) {
        *a = p.amount;
        g_written.insert(p.eid);
    }
}

void OnCanSpend(int senderSlot, const N::GasCanPayload& p) {
    if (!IsHost() || senderSlot <= 0) return;
    void* const can = EL::LiveActorOfType(static_cast<EL::ElementId>(p.eid), EL::ElementType::Prop);
    float* const a = AmountOf(can);
    if (!a || !std::isfinite(p.amount) || p.amount < 0.f || p.amount > kMaxFuel) return;
    if (p.amount >= *a) return;  // a client only spends: its word never raises a can
    UE_LOGI("gasoline_sync: HOST slot %d spent %.1f of can eid %u (%.1f left)", senderSlot, *a - p.amount, p.eid, p.amount);
    *a = p.amount;
}

void OnDisconnect() {
    if (g_spends || g_canSends || g_fuelSends || g_valvesShown)
        UE_LOGI("gasoline_sync: session end -- fuel rows sent %u, can rows sent %u; spends told %u, valves shown %u",
                g_fuelSends, g_canSends, g_spends, g_valvesShown);
    g_sentCremator.clear();
    g_sentCan.clear();
    g_hostCan.clear();
    g_written.clear();
    g_spends = g_canSends = g_fuelSends = g_valvesShown = 0;
    g_nextMs = 0;
}

}  // namespace coop::gasoline_sync
