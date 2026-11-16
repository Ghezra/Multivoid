// coop/dev/disceject_probe.cpp -- [dev] a disc put in a server box and ejected twice (VOTVCOOP_DISCEJECT_PROBE=1 on both
// peers), for Trello "Floppy disk duplication": a disc duplicates every time it is ejected from a dish's server.
//
// THE HOST, 24 s after a client is connected: spawns a disc beside its player, writes one row on it and puts it in the
// first server box through the box's own insert. THE EJECTOR (VOTVCOOP_DISCEJECT_WHO=client, the default, or host), at
// 34 s: the box's own eject. THE HOST, at 50 s: puts the one disc carrying the row back in; the ejector ejects it again
// at 60 s. EVERY PEER, every 2 s to 90 s: whether the box's slot holds the row and how many discs in the world carry it,
// with their keys (`[DISCEJECT]`). VOTVCOOP_DISCEJECT_ROWS=N writes N rows (the row, then fillers), as a disc full of
// server data holds; each disc's row count is logged with its key.

#include "coop/dev/disceject_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/actors/floppy_disc.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/devices/floppy_slot.h"
#include "ue_wrap/devices/serverbox.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace coop::dev::disceject_probe {
namespace {

namespace E = ue_wrap::engine;
namespace FD = ue_wrap::floppy_disc;
namespace FS = ue_wrap::floppy_slot;
namespace R = ue_wrap::reflection;
namespace SB = ue_wrap::serverbox;

const wchar_t* const kClass = L"prop_floppyDisc_R_C";
const wchar_t* const kRow = L"disceject probe row";

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_DISCEJECT_PROBE") == "1";
    return v;
}

bool HostEjects() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_DISCEJECT_WHO") == "host";
    return v;
}

void* Box() {
    FS::EnsureResolved(FS::DeviceKind::ServerBox);
    FD::EnsureResolved();
    SB::EnsureResolved();
    std::vector<void*> boxes;
    SB::ReadServers(boxes);
    return boxes.empty() ? nullptr : boxes[0];
}

bool Carries(void* disc) {
    FD::DiscContent c;
    if (!FD::ReadDiscContent(disc, c)) return false;
    for (const std::wstring& r : c.data)
        if (r == kRow) return true;
    return false;
}

// The live discs that carry the row, any disc class.
std::vector<void*> Carriers() {
    std::vector<void*> out;
    for (const wchar_t* cls : {L"prop_floppyDisc_C", L"prop_floppyDisc_R_C", L"prop_floppyDisc_G_C",
                               L"prop_floppyDisc_B_C", L"prop_floppyDisc_Y_C"})
        for (void* o : R::FindObjectsByClass(cls))
            if (o && R::IsLive(o) && !R::NameStartsWith(R::NameOf(o), L"Default__") && Carries(o)) {
                bool had = false;
                for (void* b : out) had = had || b == o;
                if (!had) out.push_back(o);
            }
    return out;
}

bool SlotHolds(void* box) {
    FS::Scalars st;
    FS::Content slot;
    if (!box || !FS::ReadScalars(FS::DeviceKind::ServerBox, box, st) || st.floppyType < 0 ||
        !FS::ReadContent(FS::DeviceKind::ServerBox, box, slot))
        return false;
    for (const std::wstring& r : slot.data)
        if (r == kRow) return true;
    return false;
}

uint64_t g_startAt = 0, g_nextLog = 0;
int g_step = 0;

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    const bool host = session->role() == coop::net::Role::Host;
    const bool ejector = host == HostEjects();
    if (!g_startAt) {
        g_startAt = Now();
        UE_LOGI("[DISCEJECT] probe armed role=%s ejector=%d", host ? "host" : "client", ejector ? 1 : 0);
    }
    const uint64_t t = Now() - g_startAt;
    void* const box = t >= 20000u ? Box() : nullptr;
    if (host && box && g_step == 0 && t >= 24000u) {
        g_step = 1;
        void* const me = coop::players::Registry::Get().Local();
        ue_wrap::FVector at{};
        void* disc = me && E::TryGetActorLocation(me, at)
                         ? E::SpawnActor(R::FindClass(kClass), {at.X + 100.f, at.Y, at.Z + 50.f})
                         : nullptr;
        if (disc) {
            FD::DiscContent c;
            FD::ReadDiscContent(disc, c);
            const int rows = (std::max)(1, std::atoi(coop::config::ReadEnv("VOTVCOOP_DISCEJECT_ROWS").c_str()));
            c.data.assign(1, kRow);
            for (int i = 1; i < rows; ++i)
                c.data.push_back(L"disceject filler row " + std::to_wstring(i) +
                                 L" -- 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
            FD::WriteDiscContent(disc, c);
        }
        UE_LOGI("[DISCEJECT] host: a disc spawned key=%ls and put in the box (ran=%d)",
                disc ? ue_wrap::prop::GetKeyString(disc).c_str() : L"-", disc && SB::CallProcessFloppy(box, disc) ? 1 : 0);
    }
    if (ejector && box && (g_step == 0 || g_step == 1) && t >= 34000u) {
        g_step = 2;
        UE_LOGI("[DISCEJECT] %s: the box's eject (ran=%d)", host ? "host" : "client", SB::CallEjectFloppy(box) ? 1 : 0);
    }
    if (host && box && g_step <= 2 && t >= 50000u) {
        g_step = 3;
        const std::vector<void*> discs = Carriers();
        UE_LOGI("[DISCEJECT] host: %zu discs carry the row; the first put back in (ran=%d)", discs.size(),
                !discs.empty() && SB::CallProcessFloppy(box, discs[0]) ? 1 : 0);
    }
    if (ejector && box && g_step <= 3 && t >= 60000u) {
        g_step = 4;
        UE_LOGI("[DISCEJECT] %s: the box's second eject (ran=%d)", host ? "host" : "client",
                SB::CallEjectFloppy(box) ? 1 : 0);
    }
    if (box && t < 90000u && Now() >= g_nextLog) {
        g_nextLog = Now() + 2000;
        std::wstring keys;
        const std::vector<void*> discs = Carriers();
        for (void* d : discs) {
            FD::DiscContent c;
            FD::ReadDiscContent(d, c);
            keys += ue_wrap::prop::GetKeyString(d) + L"/" + std::to_wstring(c.data.size()) + L" ";
        }
        FS::Content slot;
        FS::Scalars st;
        const size_t slotRows = FS::ReadScalars(FS::DeviceKind::ServerBox, box, st) && st.floppyType >= 0 &&
                                        FS::ReadContent(FS::DeviceKind::ServerBox, box, slot)
                                    ? slot.data.size()
                                    : 0;
        UE_LOGI("[DISCEJECT] %s t=%llu: slot holds the row %d (%zu rows), discs carrying it %zu [%ls]",
                host ? "host" : "client", t / 1000ull, SlotHolds(box) ? 1 : 0, slotRows, discs.size(), keys.c_str());
    }
}

}  // namespace coop::dev::disceject_probe
