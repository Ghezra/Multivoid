// coop/dev/beer_probe.cpp -- [dev] the beer's nausea on slot 1, watched on every peer. See the header.

#include "coop/dev/beer_probe.h"

#include "coop/config/config.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/effects.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_mainplayer.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace coop::dev::beer_probe {
namespace {

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_BEER_PROBE") == "1";
    return v;
}

int Beers() {
    static const int n = [] {
        const std::string s = coop::config::ReadEnv("VOTVCOOP_BEER_PROBE_N");
        const int v = s.empty() ? 4 : std::atoi(s.c_str());
        return v < 1 ? 1 : (v > 20 ? 20 : v);
    }();
    return n;
}

uint64_t g_startAt = 0, g_nextLog = 0;
bool g_drank = false;

std::string NauseaText() {
    std::vector<ue_wrap::effects::Entry> list;
    if (!ue_wrap::effects::List(&list)) return "unread";
    for (const auto& e : list)
        if (_wcsicmp(e.name.c_str(), L"nausea") == 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.1f for %.1fs%s", e.strength, e.time, e.live ? "" : " (gone)");
            return buf;
        }
    return "none";
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    auto& reg = coop::players::Registry::Get();
    const int mySlot = reg.LocalPeerId();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[BEER] probe armed slot=%d beers=%d", mySlot, Beers());
    }
    const uint64_t t = Now() - g_startAt;
    if (!host && mySlot == 1 && !g_drank && t >= 30000u) {
        g_drank = true;
        const int n = Beers();
        const bool ok = ue_wrap::effects::Add(L"nausea", 2.f * n, 10.f * n);
        UE_LOGI("[BEER] slot 1 drinks %d beer(s): nausea %.0f for %.0fs (ok=%d)", n, 2.f * n, 10.f * n, ok ? 1 : 0);
    }
    if (t < 25000u || t > 150000u || Now() < g_nextLog) return;
    g_nextLog = Now() + 3000u;
    bool rag = false, dead = false;
    void* const me = reg.Local();
    const bool read = me && ue_wrap::engine::ReadMainPlayerRagdollState(me, rag, dead);
    std::string peers;
    for (int s = 0; s < coop::net::kMaxPeers; ++s) {
        if (s == mySlot) continue;
        coop::RemotePlayer* const rp = reg.Puppet(static_cast<uint8_t>(s));
        if (!rp || !rp->valid()) continue;
        char buf[48];
        std::snprintf(buf, sizeof(buf), " p%d=%s", s, rp->IsRagdollDisplayed() ? "RAGDOLL" : "body");
        peers += buf;
    }
    UE_LOGI("[BEER] t=%llus slot=%d self: rag=%d dead=%d (read=%d) nausea=%s | puppets:%s",
            static_cast<unsigned long long>(t / 1000), mySlot, rag ? 1 : 0, dead ? 1 : 0, read ? 1 : 0,
            NauseaText().c_str(), peers.c_str());
}

}  // namespace coop::dev::beer_probe
