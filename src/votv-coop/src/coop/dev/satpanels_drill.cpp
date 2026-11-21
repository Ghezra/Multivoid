// coop/dev/satpanels_drill.cpp -- [dev] two SAT consoles at once, seen from three peers. See the header.

#include "coop/dev/satpanels_drill.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/desk/dish.h"
#include "ue_wrap/desk/sat_console.h"
#include "ue_wrap/devices/floppy_slot.h"
#include "ue_wrap/devices/serverbox.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <cstdint>
#include <string>

namespace coop::dev::satpanels_drill {
namespace {

namespace SC = ue_wrap::sat_console;
namespace D  = ue_wrap::dish;
namespace FS = ue_wrap::floppy_slot;
namespace E  = ue_wrap::engine;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_SATPANELS_DRILL") == "1";
    return v;
}

const char* g_who = "?";
uint64_t g_startAt = 0;  // the peer's own start edge
int g_step = 0;
void* g_a = nullptr;
void* g_b = nullptr;

// The same two consoles on every peer: the desk's list is the map's, and so are the dishes' servers.
bool Pick() {
    if (g_a && g_b) return true;
    void* panels[64];
    const size_t n = SC::Panels(panels, 64);
    void* firstServer = nullptr;
    for (size_t i = 0; i < n; ++i) {
        void* server = SC::DishServer(SC::PanelDish(panels[i]));
        if (!server) continue;
        if (!g_a) {
            g_a = panels[i];
            firstServer = server;
        } else if (server != firstServer) {
            g_b = panels[i];
            break;
        }
    }
    if (g_a && g_b)
        UE_LOGI("[SATPANELS] %s consoles: A '%ls' (server '%ls'), B '%ls' (server '%ls')", g_who,
                SC::PanelName(g_a).c_str(), ue_wrap::serverbox::ReadName(SC::DishServer(SC::PanelDish(g_a))).c_str(),
                SC::PanelName(g_b).c_str(), ue_wrap::serverbox::ReadName(SC::DishServer(SC::PanelDish(g_b))).c_str());
    return g_a && g_b;
}

// Walk up to a console as its panel's use does: the terminal records the panel and points at its dish, or ROOT.
bool Use(void* panel, bool root = false) {
    void* term = SC::LocalTerminal();
    if (!term || !panel) return false;
    void* dish = root ? nullptr : SC::PanelDish(panel);
    const int32_t idx = dish ? D::IndexOf(dish) : -1;
    return SC::WriteUsed(term, panel) && SC::CallInit(term, idx >= 0 ? D::TechName(idx) : L"ROOT", false, dish);
}

bool Type(const wchar_t* line) {
    void* term = SC::LocalTerminal();
    return term && SC::RunCommand(term, line);
}

std::wstring Screen() {
    std::wstring log;
    if (void* term = SC::LocalTerminal()) SC::ReadLog(term, log);
    return log;
}

bool Has(const std::wstring& log, const wchar_t* s) { return log.find(s) != std::wstring::npos; }

void Verdict(const char* check, bool pass, const std::wstring& log) {
    std::wstring tail = log.size() > 160 ? log.substr(log.size() - 160) : log;
    for (wchar_t& c : tail)
        if (c == L'\n' || c == L'\r') c = L'|';
    UE_LOGI("[SATPANELS] %s %s %s -- screen ends \"%ls\"", g_who, check, pass ? "PASS" : "FAIL", tail.c_str());
}

int32_t SlotType(void* server) {
    FS::Scalars sc;
    return server && FS::ReadScalars(FS::DeviceKind::ServerBox, server, sc) ? sc.floppyType : -99;
}

void HostTick(coop::net::Session* s) {
    if (!g_startAt) {
        if (!s->IsSlotWorldReady(1) || !s->IsSlotWorldReady(2) || !Pick()) return;
        g_startAt = Now();
        void* serverB = SC::DishServer(SC::PanelDish(g_b));
        if (SlotType(serverB) < 0) {
            void* cls = ue_wrap::object_index::ClassByName(L"prop_floppyDisc_Y_C");
            ue_wrap::FVector at{};
            E::TryGetActorLocation(serverB, at);
            at.Z += 50.f;
            void* disc = cls ? E::BeginDeferredSpawn(cls, at, ue_wrap::FRotator{}) : nullptr;
            if (disc && !E::FinishDeferredSpawn(disc, at, ue_wrap::FRotator{})) disc = nullptr;
            const bool in = disc && ue_wrap::serverbox::CallProcessFloppy(serverB, disc);
            UE_LOGI("[SATPANELS] host put a disc into B's server: spawned=%d inserted=%d", disc ? 1 : 0, in ? 1 : 0);
        }
        UE_LOGI("[SATPANELS] host start: A's server slot %d, B's %d", SlotType(SC::DishServer(SC::PanelDish(g_a))),
                SlotType(serverB));
        return;
    }
    const uint64_t t = Now() - g_startAt;
    if (g_step == 0 && t >= 30000u) {
        g_step = 1;
        Use(g_b);
    } else if (g_step == 1 && t >= 31000u) {
        g_step = 2;
        const std::wstring log = Screen();
        Verdict("sees-B", Has(log, L"sv.hash") && !Has(log, L"sd.cal"), log);
    } else if (g_step == 2 && t >= 35000u) {
        g_step = 3;
        const bool ok = SC::CallServerBroken(SC::LocalTerminal(), SC::DishServer(SC::PanelDish(g_a)), true);
        UE_LOGI("[SATPANELS] host raises serverBroken for A's server: ok=%d", ok ? 1 : 0);
    } else if (g_step == 3 && t >= 40000u) {
        g_step = 4;
        const std::wstring onB = Screen();
        Verdict("alert-not-on-B", !Has(onB, L"damaged"), onB);
        Use(SC::RootPanel(), true);
    } else if (g_step == 4 && t >= 41000u) {
        g_step = 5;
        const std::wstring log = Screen();
        Verdict("alert-on-root", Has(log, L"damaged"), log);
    } else if (g_step == 5 && t >= 45000u) {
        g_step = 6;
        const int32_t b = SlotType(SC::DishServer(SC::PanelDish(g_b)));
        const int32_t a = SlotType(SC::DishServer(SC::PanelDish(g_a)));
        UE_LOGI("[SATPANELS] host eject-own-server %s -- A's slot %d, B's slot %d", b >= 0 ? "PASS" : "FAIL", a, b);
        UE_LOGI("[SATPANELS] host DONE");
    }
}

void Slot1Tick() {
    if (!g_startAt) {
        coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(2);
        if (!rp || !rp->valid() || !Pick()) return;
        g_startAt = Now();
        return;
    }
    const uint64_t t = Now() - g_startAt;
    if (g_step == 0 && t >= 10000u) {
        g_step = 1;
        UE_LOGI("[SATPANELS] slot1 at A types sv.eject: use=%d type=%d", Use(g_a) ? 1 : 0, Type(L"sv.eject") ? 1 : 0);
    } else if (g_step == 1 && t >= 13000u) {
        g_step = 2;
        const std::wstring log = Screen();
        Verdict("eject-answers-A", Has(log, L"Floppy slot is empty"), log);
        UE_LOGI("[SATPANELS] slot1 at A types sd.cal: type=%d", Type(L"sd.cal") ? 1 : 0);
    }
}

void Slot2Tick() {
    if (!g_startAt) {
        if (!coop::net_pump::HasAnnouncedWorldReady() || !Pick()) return;
        g_startAt = Now();
        return;
    }
    const uint64_t t = Now() - g_startAt;
    if (g_step == 0 && t >= 15000u) {
        g_step = 1;
        UE_LOGI("[SATPANELS] slot2 at B types sv.hash: use=%d type=%d", Use(g_b) ? 1 : 0, Type(L"sv.hash") ? 1 : 0);
    } else if (g_step == 1 && t >= 20000u) {
        g_step = 2;
        const std::wstring log = Screen();
        Verdict("B-not-blocked", Has(log, L"sv.hash") && !Has(log, L"The terminal is busy"), log);
    } else if (g_step == 2 && t >= 23000u) {
        g_step = 3;
        Use(g_a);
    } else if (g_step == 3 && t >= 24000u) {
        g_step = 4;
        const std::wstring log = Screen();
        Verdict("sees-A", Has(log, L"sd.cal") && Has(log, L"recision"), log);
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const int slot = coop::players::Registry::Get().LocalPeerId();
    if (session->role() == coop::net::Role::Host) {
        g_who = "host";
        HostTick(session);
    } else if (slot == 1) {
        g_who = "slot1";
        Slot1Tick();
    } else if (slot == 2) {
        g_who = "slot2";
        Slot2Tick();
    }
}

}  // namespace coop::dev::satpanels_drill
