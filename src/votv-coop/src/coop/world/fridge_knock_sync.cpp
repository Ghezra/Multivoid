// coop/world/fridge_knock_sync.cpp -- see coop/world/fridge_knock_sync.h.
//
// 0.9.0n. fridgeGlow_C's begin play, past day 14, loops timer_3knock each second and starts timer_removeObscurity.
// timer_3knock: with `goodknock`, a 1% roll runs startKnocking unless player 0 is in `triggertooclose` or cannot see
// the fridge; else a 1% roll sets `goodknock`. startKnocking returns at once once `knocked` is set, sets `knocked` and
// `knocking`, runs setEvent(true) and, three times a second or so, PlayWorldCameraShake(TentacBallStepShake_C at the
// fridge, 100 inner, 5000 outer) and knock(true) (playKnock: audio_knock1..3), then shows the figure (`skeletalmesh`)
// and plays Timeline_0, whose end runs setEvent(false). timer_removeObscurity: with `knockamount` above 0 or a 10%
// roll, past day 7, runs knockObscurity (the props in `obscuretrigger` pushed and damaged; audio_knockobscure's volume
// and pitch from `knockamount`, then Play; `knockamount` back to 0), and sets itself again.

#include "coop/world/fridge_knock_sync.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_instances.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace coop::fridge_knock_sync {
namespace {

namespace N  = coop::net;
namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;

UE_FACT_FN(kRowKnockTimer, "fridge_knock_sync", L"fridgeGlow_C", L"timer_3knock", Declared);
UE_FACT_FN(kRowObscureTimer, "fridge_knock_sync", L"fridgeGlow_C", L"timer_removeObscurity", Declared);
constexpr int kTagKnock = 0x46524b01;     // 'FRK'
constexpr int kTagObscure = 0x46524b02;

N::Session* g_session = nullptr;
bool g_knockWatched = false, g_obscureWatched = false, g_settled = false;
bool g_running = false;          // CLIENT: this lane runs the fridge's own body for a cue
// HOST: what the fridge showed at the last look, a tenth of a second apart: a knock that started since is sent.
bool g_knockedSeen = false;
int32_t g_amountSeen = 0;
uint32_t g_sent = 0, g_played = 0, g_refused = 0;

bool IsHost() { return g_session && g_session->running() && g_session->role() == N::Role::Host; }
bool IsClient() { return g_session && g_session->running() && g_session->role() == N::Role::Client; }

void* Fridge() {
    void* one[1];
    return ue_wrap::world_instances::Find(L"fridgeGlow_C", one, 1) > 0 && R::IsLive(one[0]) ? one[0] : nullptr;
}

bool ReadFlag(void* obj, const wchar_t* name) {
    int32_t off = -1;
    uint8_t mask = 0;
    return obj && R::FindBoolProperty(R::ClassOf(obj), name, off, mask) && off >= 0 &&
           (static_cast<uint8_t*>(obj)[off] & mask) != 0;
}

int32_t ReadInt(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    int32_t v = 0;
    if (off >= 0) std::memcpy(&v, static_cast<char*>(obj) + off, sizeof(v));
    return v;
}

void* ObjField(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    void* v = nullptr;
    if (off >= 0) std::memcpy(&v, static_cast<char*>(obj) + off, sizeof(v));
    return v && R::IsLive(v) ? v : nullptr;
}

float ReadFloat(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    float v = 1.f;
    if (off >= 0) std::memcpy(&v, static_cast<char*>(obj) + off, sizeof(v));
    return v;
}

bool CallFloat(void* obj, const wchar_t* fn, const wchar_t* param, float v) {
    ue_wrap::ParamFrame f(obj ? R::FindDispatchFunctionCached(R::ClassOf(obj), fn) : nullptr);
    return f.valid() && f.Set<float>(param, v) && ue_wrap::Call(obj, f);
}

void SendCue(uint32_t cue, float x, float y) {
    N::EventCuePayload p{};
    p.cueId = cue;
    p.x = x;
    p.y = y;
    if (g_session->SendReliable(N::ReliableKind::EventCue, &p, sizeof(p))) ++g_sent;
}

// The fridge's two timers are each player's own dice: a client's are refused, so only the host's fridge knocks.
sg::Verdict OnTimerPre(const sg::Call&) {
    if (!IsClient() || g_running) return sg::Verdict::Run;
    ++g_refused;
    return sg::Verdict::Cancel;
}

// HOST: the fridge as it stands, against the last look: `knocked` newly set is the big knock started (once a session),
// `knockamount` spent to 0 the quiet knock played, its sound's volume and pitch read off its audio.
void HostLook() {
    void* const fridge = Fridge();
    if (!fridge) return;
    const bool knocked = ReadFlag(fridge, L"knocked");
    const int32_t amount = ReadInt(fridge, L"knockamount");
    if (knocked && !g_knockedSeen) {
        SendCue(kCueKnocking, 0.f, 0.f);
        UE_LOGI("fridge_knock_sync: HOST the basement fridge's knocking started -- sent");
    }
    if (g_amountSeen > 0 && amount == 0) {
        void* const audio = ObjField(fridge, L"audio_knockobscure");
        const float vol = ReadFloat(audio, L"VolumeMultiplier"), pitch = ReadFloat(audio, L"PitchMultiplier");
        SendCue(kCueObscure, vol, pitch);
        UE_LOGI("fridge_knock_sync: HOST the basement fridge's quiet knock (%d knocks' worth, volume %.2f, pitch %.2f) "
                "-- sent", g_amountSeen, vol, pitch);
    }
    g_knockedSeen = knocked;
    g_amountSeen = amount;
}

}  // namespace

void Install(N::Session* session) { g_session = session; }

void Tick() {
    if (!g_knockWatched && sg::WatchName(kRowKnockTimer, kTagKnock, &OnTimerPre, nullptr)) g_knockWatched = true;
    if (!g_obscureWatched && sg::WatchName(kRowObscureTimer, kTagObscure, &OnTimerPre, nullptr)) g_obscureWatched = true;
    // Every tick: two field reads, and a knock is sent the frame it starts, not up to a tenth of a second later.
    if (IsHost()) HostLook();
    if (!g_knockWatched || !g_obscureWatched || g_settled) return;
    sg::ResolvePendingNames();
    if (!sg::NameWatchSettled(kRowKnockTimer, kTagKnock) || !sg::NameWatchSettled(kRowObscureTimer, kTagObscure)) return;
    g_settled = true;
    const bool live = sg::NameWatchLive(kRowKnockTimer, kTagKnock) && sg::NameWatchLive(kRowObscureTimer, kTagObscure);
    if (live) UE_LOGI("fridge_knock_sync: the watches on the basement fridge's knock timers are live");
    else UE_LOGE("fridge_knock_sync: a fridge knock timer watch is not live -- each player's fridge knocks on its own");
}

bool OnCue(const N::EventCuePayload& p) {
    if (p.cueId != kCueKnocking && p.cueId != kCueObscure) return false;
    if (!IsClient()) return true;
    void* const fridge = Fridge();
    if (!fridge) {
        UE_LOGW("fridge_knock_sync: CLIENT a fridge knock (cue %u) with no basement fridge here", p.cueId);
        return true;
    }
    g_running = true;
    bool ran = false;
    if (p.cueId == kCueKnocking) {
        ue_wrap::ParamFrame f(R::FindDispatchFunctionCached(R::ClassOf(fridge), L"startKnocking"));
        ran = f.valid() && ue_wrap::Call(fridge, f);
    } else if (void* const audio = ObjField(fridge, L"audio_knockobscure")) {
        CallFloat(audio, L"SetVolumeMultiplier", L"NewVolumeMultiplier", p.x);
        CallFloat(audio, L"SetPitchMultiplier", L"NewPitchMultiplier", p.y);
        ran = CallFloat(audio, L"Play", L"StartTime", 0.f);
    }
    g_running = false;
    ++g_played;
    UE_LOGI("fridge_knock_sync: CLIENT the host's %s (ran=%d)",
            p.cueId == kCueKnocking ? "fridge knocking" : "quiet fridge knock", ran ? 1 : 0);
    return true;
}

void OnDisconnect() {
    if (g_sent || g_played || g_refused)
        UE_LOGI("fridge_knock_sync: session end -- host knocks sent %u; client knocks played %u, own timers refused %u",
                g_sent, g_played, g_refused);
    g_sent = g_played = g_refused = 0;
    g_running = false;
    g_knockedSeen = false;
    g_amountSeen = 0;
}

}  // namespace coop::fridge_knock_sync
