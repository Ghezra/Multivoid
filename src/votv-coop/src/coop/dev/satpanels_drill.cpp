// coop/dev/satpanels_drill.cpp -- [dev] two SAT consoles at once, seen from three peers. See the header.

#include "coop/dev/satpanels_drill.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/desk/device_screen.h"
#include "ue_wrap/desk/dish.h"
#include "ue_wrap/desk/phys_mods.h"
#include "ue_wrap/desk/sat_console.h"
#include "ue_wrap/devices/floppy_slot.h"
#include "ue_wrap/devices/serverbox.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/hit_result.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace coop::dev::satpanels_drill {
namespace {

namespace SC = ue_wrap::sat_console;
namespace D  = ue_wrap::dish;
namespace FS = ue_wrap::floppy_slot;
namespace E  = ue_wrap::engine;
namespace R  = ue_wrap::reflection;

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
uint8_t g_mods[ue_wrap::phys_mods::kSlots] = {};  // HOST: the desk's modules before the alert's
bool g_modsSaved = false;

// The same two consoles on every peer: the desk's list is the map's, and so are the dishes' servers.
bool Pick() {
    if (g_a && g_b) return true;
    if (!ue_wrap::serverbox::EnsureResolved() || !FS::EnsureResolved(FS::DeviceKind::ServerBox)) return false;
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

// Look at a console without entering it, as the host here does: the terminal records the panel and points at its
// dish, or at ROOT.
bool Use(void* panel, bool root = false) {
    void* term = SC::LocalTerminal();
    if (!term || !panel) return false;
    void* dish = root ? nullptr : SC::PanelDish(panel);
    const int32_t idx = dish ? D::IndexOf(dish) : -1;
    return SC::WriteUsed(term, panel) && SC::CallInit(term, idx >= 0 ? D::TechName(idx) : L"ROOT", false, dish);
}

// Enter a console as a player's use does: the panel's actionOptionIndex, action 4, with a hit on its box.
bool Enter(void* panel) {
    void* me = coop::players::Registry::Get().Local();
    void* fn = panel ? R::FindFunction(R::ClassOf(panel), L"actionOptionIndex") : nullptr;
    const int32_t boxOff = panel ? R::FindPropertyOffset(R::ClassOf(panel), L"box") : -1;
    void* box = nullptr;
    if (boxOff >= 0) std::memcpy(&box, static_cast<uint8_t*>(panel) + boxOff, sizeof(void*));
    if (!me || !fn || !box) return false;
    ue_wrap::FVector at{};
    E::TryGetActorLocation(panel, at);
    ue_wrap::ParamFrame f(fn);
    const uint8_t action = 4;
    void* none = nullptr;
    if (!f.valid() || !f.Set(L"player", me) || !ue_wrap::hit_result::Write(f, L"hit", panel, box, at) ||
        !f.SetRaw(L"action", &action, sizeof(action)) || !f.Set(L"lookAtComponent", none))
        return false;
    return ue_wrap::Call(panel, f);
}

bool Inside() {
    void* me = coop::players::Registry::Get().Local();
    return me && ue_wrap::device_screen::ReadActiveInterface(me) != nullptr;
}

void Leave() {
    if (void* me = coop::players::Registry::Get().Local()) ue_wrap::device_screen::ForceExitInterface(me);
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
    if (g_step == 0 && t >= 35000u) {
        g_step = 1;
        Use(g_a);
    } else if (g_step == 1 && t >= 36000u) {
        g_step = 2;
        const std::wstring log = Screen();
        Verdict("sees-A", Has(log, L"sd.calall"), log);
        Use(g_b);
    } else if (g_step == 2 && t >= 37000u) {
        g_step = 3;
        const std::wstring log = Screen();
        Verdict("sees-B", Has(log, L"sd.calch") && !Has(log, L"sd.calall"), log);
    } else if (g_step == 3 && t >= 40000u) {
        g_step = 4;
        // The alert prints only on a desk with the server alert module (physMods holds 1): lent for the alert.
        namespace PM = ue_wrap::phys_mods;
        uint8_t mods[PM::kSlots] = {};
        bool lent = false;
        if (PM::EnsureResolved() && PM::ReadArray(mods)) {
            std::memcpy(g_mods, mods, sizeof(mods));
            g_modsSaved = true;
            bool has = false;
            for (uint8_t m : mods) has = has || m == 1;
            for (uint8_t& m : mods)
                if (!has && m == 0) { m = 1; has = lent = true; }
            if (lent) PM::WriteArray(mods);
        }
        const bool ok = SC::CallServerBroken(SC::LocalTerminal(), SC::DishServer(SC::PanelDish(g_a)), true);
        UE_LOGI("[SATPANELS] host raises serverBroken for A's server: ok=%d (alert module lent=%d)", ok ? 1 : 0,
                lent ? 1 : 0);
    } else if (g_step == 4 && t >= 45000u) {
        g_step = 5;
        const std::wstring onB = Screen();
        Verdict("alert-not-on-B", !Has(onB, L"damaged"), onB);
        Use(SC::RootPanel(), true);
    } else if (g_step == 5 && t >= 46000u) {
        g_step = 6;
        const std::wstring log = Screen();
        Verdict("alert-on-root", Has(log, L"damaged"), log);
        if (g_modsSaved) ue_wrap::phys_mods::WriteArray(g_mods);  // the desk's own modules back
    } else if (g_step == 6 && t >= 50000u) {
        g_step = 7;
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
        const bool entered = Enter(g_a);
        UE_LOGI("[SATPANELS] slot1 enters A: call=%d", entered ? 1 : 0);
    } else if (g_step == 1 && t >= 12000u) {
        g_step = 2;
        const bool inside = Inside();
        const bool typed = Type(L"sv.eject");
        UE_LOGI("[SATPANELS] slot1 inside A=%d, types sv.eject: type=%d", inside ? 1 : 0, typed ? 1 : 0);
    } else if (g_step == 2 && t >= 15000u) {
        g_step = 3;
        const std::wstring log = Screen();
        Verdict("eject-answers-A", Has(log, L"Floppy slot is empty"), log);
        const bool typed = Type(L"sd.calall");
        UE_LOGI("[SATPANELS] slot1 at A types sd.calall: type=%d", typed ? 1 : 0);
    } else if (g_step == 3 && t >= 60000u) {
        g_step = 4;
        Leave();
        UE_LOGI("[SATPANELS] slot1 leaves A");
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
        const bool entered = Enter(g_a);
        UE_LOGI("[SATPANELS] slot2 enters A, which slot 1 uses: call=%d", entered ? 1 : 0);
    } else if (g_step == 1 && t >= 18000u) {
        g_step = 2;
        const bool inside = Inside();
        UE_LOGI("[SATPANELS] slot2 same-console-blocked %s -- inside an interface=%d", inside ? "FAIL" : "PASS",
                inside ? 1 : 0);
        if (inside) Leave();
    } else if (g_step == 2 && t >= 19000u) {
        g_step = 3;
        const bool entered = Enter(g_b);
        UE_LOGI("[SATPANELS] slot2 enters B: call=%d", entered ? 1 : 0);
    } else if (g_step == 3 && t >= 22000u) {
        g_step = 30;
        const bool inside = Inside();
        UE_LOGI("[SATPANELS] slot2 other-console-free %s -- inside B=%d", inside ? "PASS" : "FAIL", inside ? 1 : 0);
        if (!inside) {
            const bool entered = Enter(g_b);
            UE_LOGI("[SATPANELS] slot2 enters B again: call=%d", entered ? 1 : 0);
        }
    } else if (g_step == 30 && t >= 25000u) {
        g_step = 4;
        const bool inside = Inside();
        UE_LOGI("[SATPANELS] slot2 other-console-free (by its second entry) %s -- inside B=%d",
                inside ? "PASS" : "FAIL", inside ? 1 : 0);
        const bool typed = Type(L"sd.calch");
        UE_LOGI("[SATPANELS] slot2 at B types sd.calch: type=%d", typed ? 1 : 0);
    } else if (g_step == 4 && t >= 29000u) {
        g_step = 5;
        const std::wstring log = Screen();
        Verdict("info-at-same-time", Has(log, L"sd.calch") && !Has(log, L"is running") && !Has(log, L"busy"), log);
        const bool typed = Type(L"sd.calall");
        UE_LOGI("[SATPANELS] slot2 at B types sd.calall: type=%d", typed ? 1 : 0);
    } else if (g_step == 5 && t >= 32000u) {
        g_step = 6;
        const std::wstring log = Screen();
        Verdict("mutating-locked", Has(log, L"is running sd.calall"), log);
    } else if (g_step == 6 && t >= 35000u) {
        g_step = 7;
        Leave();
        UE_LOGI("[SATPANELS] slot2 leaves B");
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
