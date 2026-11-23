// coop/player/puppet_gravgun.cpp -- see coop/player/puppet_gravgun.h.
//
// 0.9.0n. comp_gravitygun_C::ReceiveTick: its `recharge` counts down; then, while getMainPlayer's holding_actor is a
// prop_gravgun_C, with that player's grabbing_actor set and the recharge spent, it holds or shoots (trace, attract)
// and sets the recharge to 0.5 or 1 s.

#include "coop/player/puppet_gravgun.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"

#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"

#include <atomic>

namespace coop::puppet_gravgun {
namespace {

namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;

UE_FACT_FN(kRowTick, "puppet_gravgun", L"comp_gravitygun_C", L"ReceiveTick", Declared);
constexpr int kTag = 0x50475254;   // 'PGRT'

std::atomic<coop::net::Session*> g_session{nullptr};
bool g_watched = false;
std::atomic<unsigned> g_refused{0};

// [dev] week6_red=gravgun_local, the gravity gun drill's red arm: every player's component ticks, as before.
bool RedLocal() {
    static const bool s_red = coop::config::ResolveEnum(coop::config_registry::rows::week6_red) == "gravgun_local";
    return s_red;
}

sg::Verdict OnTickPre(const sg::Call& call) {
    if (call.tag != kTag || !call.object || RedLocal()) return sg::Verdict::Run;
    void* const owner = R::OuterOf(call.object);   // the component's actor: its player
    if (!owner || !coop::players::Registry::Get().IsPuppet(owner)) return sg::Verdict::Run;
    if (++g_refused == 1) UE_LOGI("puppet_gravgun: a puppet's gravity gun component does not shoot for this player");
    return sg::Verdict::Cancel;
}

}  // namespace

void Install(coop::net::Session* session) { g_session.store(session, std::memory_order_release); }

void Tick() {
    coop::net::Session* const s = g_session.load(std::memory_order_acquire);
    if (!s || !s->running() || g_watched) return;
    if ((g_watched = sg::WatchName(kRowTick, kTag, &OnTickPre, nullptr)))
        UE_LOGI("puppet_gravgun: the gravity gun component's tick is watched");
}

unsigned RefusedTicks() { return g_refused.load(std::memory_order_relaxed); }

}  // namespace coop::puppet_gravgun
