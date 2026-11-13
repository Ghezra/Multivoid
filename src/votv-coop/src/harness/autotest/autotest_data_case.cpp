// harness/autotest/autotest_data_case.cpp -- TEST-ONLY: a drive box (prop_box_C, the data case) a client fills.
//
// The host spawns an empty box and names its key. The client fills every slot of its copy with a level-3 signal through
// the box's own putDriveIn, then publishes the box's record as its own upd would (a drill's call is ours, so the record
// watch skips it), and logs the record's size against the transport's ceiling. The host then reads its own copy's record,
// the one a drone sale prices, and logs each signal's name and level; the client reads its copy after the host's answer.
// A file per step orders the two peers. Gated by VOTVCOOP_RUN_DATA_CASE; the folder is VOTVCOOP_CAN_DIR.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "coop/creatures/npc_sync.h"       // GetSession
#include "coop/net/blob_chunks.h"          // MaxBlobBytes, the record ceiling
#include "coop/player/players_registry.h"
#include "coop/props/prop_save_data.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/save_record.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/desk/drive_chain.h"
#include "ue_wrap/desk/saved_signals.h"
#include "ue_wrap/engine/engine.h"

#include <windows.h>

#include <atomic>
#include <fstream>
#include <string>

namespace harness::autotest {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace DC = ue_wrap::drive_chain;
namespace SR = ue_wrap::save_record;

constexpr const wchar_t* kBoxPath = L"/Game/objects/prop_box.prop_box_C";

std::string g_dir;
void* g_box = nullptr;   // game thread only

template <class Fn>
void OnGameThread(Fn&& fn) {
    ue_wrap::game_thread::RunAndWait([&fn](std::atomic<int>& d) { fn(); d.store(1); });
}

void Write(const char* name, const std::string& text) {
    std::ofstream(g_dir + "\\" + name, std::ios::binary | std::ios::trunc) << text;
}

bool Await(const char* name, std::string* text = nullptr, int seconds = 240) {
    for (int i = 0; i < seconds * 10; ++i) {
        std::ifstream f(g_dir + "\\" + name, std::ios::binary);
        std::string s;
        if (f && std::getline(f, s) && !s.empty()) {
            if (text) *text = s;
            return true;
        }
        ::Sleep(100);
    }
    UE_LOGW("data-case: VERDICT ABORT -- no '%hs' from the other peer", name);
    return false;
}

// A TArray property's element count and data; false when it does not read.
bool ArrayOf(void* obj, const wchar_t* name, const uint8_t** data, int32_t* num) {
    const int32_t off = obj && R::IsLive(obj) ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    if (off < 0) return false;
    const uint8_t* base = static_cast<const uint8_t*>(obj) + off;
    *data = *reinterpret_cast<uint8_t* const*>(base);
    *num = *reinterpret_cast<const int32_t*>(base + 8);
    return *num >= 0 && (*num == 0 || *data);
}

// drives_in: the slot count and how many hold a drive.
void Slots(void* box, int32_t& slots, int32_t& filled) {
    const uint8_t* d = nullptr;
    slots = -1;
    filled = 0;
    if (!ArrayOf(box, L"drives_in", &d, &slots)) return;
    for (int32_t i = 0; i < slots; ++i) filled += d[i] != 0 ? 1 : 0;
}

void* SlotComp(void* box, int i) {
    const uint8_t* d = nullptr;
    int32_t n = 0;
    if (!ArrayOf(box, L"boxesTr", &d, &n) || i >= n) return nullptr;
    void* c = reinterpret_cast<void* const*>(d)[i];
    return c && R::IsLive(c) ? c : nullptr;
}

// The box's record, as getData makes it: what a container files and the drone's sale prices.
void LogRecord(const char* who, void* box) {
    int32_t slots = -1, filled = 0;
    Slots(box, slots, filled);
    SR::SaveRecord rec;
    if (!box || !SR::CaptureRecord(box, rec)) {
        UE_LOGW("data-case: %hs the box's record did not read", who);
        return;
    }
    std::string levels;
    int photos = 0;
    for (const auto& s : rec.signals) {
        char one[64];
        std::snprintf(one, sizeof(one), "%ls:%d ", s.name.c_str(), s.level);
        levels += one;
        photos += s.image.empty() ? 0 : 1;
    }
    UE_LOGI("data-case: %hs RECORD slots=%d filled=%d signals=%zu photos=%d levels=%hs", who, slots, filled,
            rec.signals.size(), photos, levels.c_str());
}

void* FindBox(const std::wstring& key) {
    for (void* o : R::FindObjectsByClass(L"prop_box_C"))
        if (o && R::IsLive(o) && ue_wrap::prop::GetInteractableKeyString(o) == key) return o;
    return nullptr;
}

void RunHost() {
    ::Sleep(20000);
    if (!Await("client_ready")) return;
    std::string key;
    OnGameThread([&] {
        void* me = coop::players::Registry::Get().Local();
        ue_wrap::FVector at{};
        if (!me || !E::TryGetActorLocation(me, at)) return;
        const ue_wrap::FVector fwd = E::GetActorForwardVector(me);
        void* cls = ue_wrap::asset_load::LoadObjectByPath(kBoxPath);
        g_box = cls ? E::SpawnActor(cls, {at.X + fwd.X * 120.f, at.Y + fwd.Y * 120.f, at.Z + 40.f}) : nullptr;
    });
    if (!g_box) {
        UE_LOGW("data-case: VERDICT ABORT -- the host's box did not spawn");
        return;
    }
    for (int i = 0; i < 100 && key.empty(); ++i) {   // the key is minted in the construction script
        OnGameThread([&] {
            const std::wstring k = ue_wrap::prop::GetInteractableKeyString(g_box);
            if (!k.empty() && k != L"None") key.assign(k.begin(), k.end());
        });
        if (key.empty()) ::Sleep(100);
    }
    ::Sleep(5000);   // the spawn reaches the client
    OnGameThread([&] { LogRecord("HOST before", g_box); });
    Write("box_key", key);
    std::string packed;
    if (!Await("client_packed", &packed)) return;
    ::Sleep(4000);
    OnGameThread([&] { LogRecord("HOST after", g_box); });
    Write("host_read", "x");
    if (!Await("client_done")) return;
    UE_LOGI("data-case: DONE");
}

void RunClient() {
    bool ready = false;
    for (int i = 0; i < 480 && !ready; ++i) {
        OnGameThread([&] { ready = coop::net_pump::HasAnnouncedWorldReady(); });
        if (!ready) ::Sleep(500);
    }
    ::Sleep(10000);
    Write("client_ready", "x");
    std::string key;
    if (!Await("box_key", &key)) return;
    const std::wstring wkey(key.begin(), key.end());
    for (int i = 0; i < 300 && !g_box; ++i) {
        OnGameThread([&] { g_box = FindBox(wkey); });
        if (!g_box) ::Sleep(100);
    }
    if (!g_box) {
        UE_LOGW("data-case: VERDICT ABORT -- the client never had the box keyed '%hs'", key.c_str());
        return;
    }
    std::string packed = "0";
    OnGameThread([&] {
        LogRecord("CLIENT before", g_box);
        int32_t slots = -1, filled = 0;
        Slots(g_box, slots, filled);
        std::wstring signal;
        ue_wrap::saved_signals::FirstOrdinarySignalName(signal);
        void* put = R::FindFunction(R::ClassOf(g_box), L"putDriveIn");
        ue_wrap::FVector at{};
        if (!put || !DC::EnsureResolved() || !E::TryGetActorLocation(g_box, at)) {
            UE_LOGW("data-case: CLIENT cannot fill the box (putDriveIn %p)", put);
            return;
        }
        int took = 0;
        for (int32_t i = 0; i < slots; ++i) {
            void* drive = E::SpawnActor(DC::DriveClass(), {at.X, at.Y, at.Z + 60.f});
            ue_wrap::signal_dynamic::Row row;
            if (!drive || !DC::ReadDriveRow(drive, row)) continue;
            row.name = L"dc" + std::to_wstring(i);
            row.id = L"dc-" + std::to_wstring(i);
            if (!signal.empty()) row.signal = signal;
            row.level = 3;
            row.size = 10.f;
            row.decoded = 10.f;
            row.quality = 2;
            row.hasData = true;
            ue_wrap::ParamFrame f(put);
            if (!DC::WriteDriveRow(drive, row) || !DC::CallDriveUpd(drive) || !f.valid() ||
                !f.Set<void*>(L"Object", drive) || !f.Set<void*>(L"lookAt", SlotComp(g_box, i)) ||
                !f.Set<bool>(L"ignoreNotif", true) || !ue_wrap::Call(g_box, f)) {
                UE_LOGW("data-case: CLIENT slot %d: the drive or putDriveIn did not run", i);
                continue;
            }
            ++took;
        }
        Slots(g_box, slots, filled);
        // The publish the box's own upd makes on a player's press (prop_record_refresh): the same call, as the client's
        // intent to the host.
        size_t bytes = 0;
        const bool sent = coop::prop_save_data::Publish(coop::npc_sync::GetSession(), g_box, wkey, &bytes);
        UE_LOGI("data-case: CLIENT packed %d of %d slots (filled=%d), record %zu B of the %zu B ceiling, sent=%d", took,
                slots, filled, bytes, coop::blob_chunks::MaxBlobBytes(), sent ? 1 : 0);
        LogRecord("CLIENT packed", g_box);
        packed = std::to_string(bytes);
    });
    Write("client_packed", packed);
    if (!Await("host_read")) return;
    ::Sleep(2000);
    OnGameThread([&] { LogRecord("CLIENT after", g_box); });
    Write("client_done", "x");
    UE_LOGI("data-case: DONE");
}

}  // namespace

DWORD WINAPI DataCaseThread(LPVOID /*arg*/) {
    g_dir = coop::config::ReadEnv("VOTVCOOP_CAN_DIR");
    if (g_dir.empty()) {
        UE_LOGW("data-case: VERDICT ABORT -- VOTVCOOP_CAN_DIR is unset");
        return 0;
    }
    if (IsClientRole()) RunClient();
    else                RunHost();
    return 0;
}

}  // namespace harness::autotest
