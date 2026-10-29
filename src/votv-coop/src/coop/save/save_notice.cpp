// coop/save/save_notice.cpp -- see coop/save/save_notice.h.

#include "coop/save/save_notice.h"

#include "coop/comms/peer_action_feed.h"  // the line, in the player's language
#include "coop/net/protocol.h"
#include "coop/net/session.h"

#include "ue_wrap/actors/inventory.h"           // ResolveSaveSlot: the hosted world's save object
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"      // FunctionParams
#include "ue_wrap/engine/save_to_slot_hook.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/world_singleton.h"      // Gamemode

#include <atomic>
#include <cstdint>
#include <cwchar>

namespace coop::save_notice {
namespace {

namespace R = ue_wrap::reflection;

UE_FACT_FN(kRowSaveAnim, "save_notice", L"mainGamemode_C", L"saveAnim", Declared);

std::atomic<coop::net::Session*> g_session{nullptr};
bool     g_subscribed = false;
uint32_t g_saves = 0;            // HOST: the world saves this session
uint32_t g_heard = 0;            // CLIENT: the host's saves this session
bool     g_animSaid = false;     // CLIENT: a save animation that could not play is said once

// HOST: the engine wrote a world save. Only THE world counts -- the live save object under a slot of the
// game's own: the join capture writes it to a zcoop_ scratch slot, and the save-slot menu writes save objects
// of its own; neither is the hosted world saved.
void OnWorldSaveWritten(void* saveObject, const wchar_t* slotName) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Host) return;
    if (std::wcsncmp(slotName, L"zcoop_", 6) == 0) return;
    if (saveObject != ue_wrap::inventory::ResolveSaveSlot()) return;
    coop::net::WorldSavedPayload p{};
    p.saveNumber = ++g_saves;
    const bool sent = s->SendReliable(coop::net::ReliableKind::WorldSaved, &p, sizeof(p));
    coop::peer_action_feed::AnnounceDirect(0, coop::peer_action_feed::Action::SavedGame);
    UE_LOGI("save_notice: the host's world was saved to '%ls' (#%u) -- WorldSaved %s", slotName, g_saves,
            sent ? "sent" : "had no peer");
}

// CLIENT: the game's own save animation, on this peer's gamemode. It is called only as the game calls it, with
// no arguments; a function that takes any is not the animation this lane knows, and is left alone.
void PlaySaveAnim() {
    void* const gm = ue_wrap::world_singleton::Gamemode();
    const ue_wrap::facts::Resolved fn = ue_wrap::facts::Resolve(kRowSaveAnim);
    const char* why = nullptr;
    if (!gm) why = "no gamemode";
    else if (!fn) why = "mainGamemode.saveAnim did not resolve";
    else if (!R::FunctionParams(fn.Fn()).empty()) why = "mainGamemode.saveAnim takes arguments";
    else if (!R::CallFunction(gm, fn.Fn(), nullptr)) why = "the call did not dispatch";
    if (why && !g_animSaid) {
        g_animSaid = true;
        UE_LOGW("save_notice: the host's save is shown in the chat only -- %s (said once)", why);
    }
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    if (!g_subscribed) g_subscribed = ue_wrap::save_to_slot_hook::AddWritten(&OnWorldSaveWritten);
}

void OnWorldSaved(const coop::net::WorldSavedPayload& p) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || s->role() != coop::net::Role::Client) return;
    ++g_heard;
    coop::peer_action_feed::AnnounceDirect(0, coop::peer_action_feed::Action::SavedGame);
    PlaySaveAnim();
    UE_LOGI("save_notice: the host saved the world (its #%u, heard #%u)", p.saveNumber, g_heard);
}

void OnDisconnect() {
    if (g_saves || g_heard) UE_LOGI("save_notice: session end -- saved=%u heard=%u", g_saves, g_heard);
    g_saves = 0;
    g_heard = 0;
    g_animSaid = false;
}

}  // namespace coop::save_notice
