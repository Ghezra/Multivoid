// harness/autotest/autotest_class_dump.cpp -- TEST-ONLY: what a Blueprint class is, read from the running game.
//
// For each name in VOTVCOOP_CLASS_DUMP (comma separated, no _C), the class is loaded from /Game/objects/ and its
// parent chain, its own fields and its own functions are logged, with each function's calls in bytecode order. The
// reverse-engineering read a new sync lane starts from, on one peer, needing no session. Greppable as
// "class_dump:". Gated by VOTVCOOP_RUN_CLASS_DUMP.

#include "harness/autotest.h"

#include "coop/config/config.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_calls.h"
#include "ue_wrap/core/script_fingerprint.h"


#include <windows.h>

#include <atomic>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace harness::autotest {
namespace {

namespace R  = ue_wrap::reflection;
namespace SC = ue_wrap::script_calls;

std::wstring CalleeName(const SC::CallSite& s) {
    if (s.callee) return R::ToString(R::NameOf(s.callee));
    R::FName n{};
    n.ComparisonIndex = static_cast<int32_t>(s.nameComparisonIndex);
    n.Number = static_cast<int32_t>(s.nameNumber);
    return R::ToString(n);
}

void DumpClass(const std::wstring& name) {
    const std::wstring path = L"/Game/objects/" + name + L"." + name + L"_C";
    void* cls = ue_wrap::asset_load::LoadObjectByPath(path.c_str());
    if (!cls) { UE_LOGW("class_dump: %ls did not load", path.c_str()); return; }
    std::wstring chain;
    for (void* c = R::SuperStructOf(cls); c; c = R::SuperStructOf(c)) chain += L" < " + R::ToString(R::NameOf(c));
    UE_LOGI("class_dump: == %ls%ls", name.c_str(), chain.c_str());
    for (const R::StructFieldInfo& f : R::EnumerateStructFields(cls))
        UE_LOGI("class_dump: %ls field %ls off=0x%x size=%d", name.c_str(), f.name.c_str(), f.offset, f.size);
    for (const R::ObjectRef& o : R::ChildObjectsOf(cls)) {
        if (o.className != L"Function") continue;
        std::vector<SC::CallSite> calls;
        std::wstring list;
        if (SC::EnumerateCalls(o.object, calls))
            for (const SC::CallSite& s : calls) list += L" " + CalleeName(s);
        UE_LOGI("class_dump: %ls fn %ls calls:%ls", name.c_str(), o.name.c_str(), list.c_str());
    }
}

}  // namespace

DWORD WINAPI ClassDumpThread(LPVOID /*arg*/) {
    const std::string list = coop::config::ReadEnv("VOTVCOOP_CLASS_DUMP");
    ::Sleep(15000);   // the world up: the class set is what the game loads
    // A substring search over every loaded Blueprint class's own fields and functions, for a state whose class is
    // not yet known. VOTVCOOP_CLASS_GREP, lower case.
    const std::string grep = coop::config::ReadEnv("VOTVCOOP_CLASS_GREP");
    if (!grep.empty()) {
        const std::wstring needle(grep.begin(), grep.end());
        auto has = [&needle](std::wstring s) {
            for (wchar_t& c : s) c = static_cast<wchar_t>(::towlower(c));
            return s.find(needle) != std::wstring::npos;
        };
        ue_wrap::game_thread::RunAndWait([&](std::atomic<int>& d) {
            for (void* cls : R::FindObjectsByClass(L"BlueprintGeneratedClass")) {
                const std::wstring name = R::ToString(R::NameOf(cls));
                if (has(name)) UE_LOGI("class_dump: grep class %ls", name.c_str());
                for (const R::StructFieldInfo& f : R::EnumerateStructFields(cls))
                    if (has(f.name)) UE_LOGI("class_dump: grep %ls field %ls size=%d", name.c_str(), f.name.c_str(), f.size);
                for (const R::ObjectRef& o : R::ChildObjectsOf(cls))
                    if (o.className == L"Function" && has(o.name)) UE_LOGI("class_dump: grep %ls fn %ls", name.c_str(), o.name.c_str());
            }
            d.store(1);
        });
    }
    // Every function of one loaded class as readable bytecode text, to a file: the way to read which body writes
    // a field. VOTVCOOP_CLASS_SCRIPT names the class, VOTVCOOP_CLASS_SCRIPT_OUT the file.
    const std::string scriptCls = coop::config::ReadEnv("VOTVCOOP_CLASS_SCRIPT");
    const std::string scriptOut = coop::config::ReadEnv("VOTVCOOP_CLASS_SCRIPT_OUT");
    if (!scriptCls.empty() && !scriptOut.empty()) {
        const std::wstring w(scriptCls.begin(), scriptCls.end());
        ue_wrap::game_thread::RunAndWait([&](std::atomic<int>& d) {
            std::ofstream out(scriptOut, std::ios::binary | std::ios::trunc);
            void* cls = R::FindClass(w.c_str());
            if (cls) {
                for (const R::ObjectRef& o : R::ChildObjectsOf(cls)) {
                    if (o.className != L"Function") continue;
                    const ue_wrap::script_fingerprint::Result r = ue_wrap::script_fingerprint::Of(o.object);
                    out << "=== " << std::string(o.name.begin(), o.name.end()) << '\n' << r.text << '\n';
                }
            }
            UE_LOGI("class_dump: script of %ls written (class %hs)", w.c_str(), cls ? "found" : "NOT loaded");
            d.store(1);
        });
    }
    // The same text for classes nothing has loaded yet, in one launch: VOTVCOOP_CLASS_SCRIPT_PATHS lists object paths,
    // comma separated, and each lands as <VOTVCOOP_CLASS_SCRIPT_DIR>/<class>_script.txt.
    const std::string scriptPaths = coop::config::ReadEnv("VOTVCOOP_CLASS_SCRIPT_PATHS");
    const std::string scriptDir = coop::config::ReadEnv("VOTVCOOP_CLASS_SCRIPT_DIR");
    if (!scriptPaths.empty() && !scriptDir.empty()) {
        std::stringstream paths(scriptPaths);
        std::string path;
        while (std::getline(paths, path, ',')) {
            const std::wstring w(path.begin(), path.end());
            ue_wrap::game_thread::RunAndWait([&](std::atomic<int>& d) {
                void* cls = ue_wrap::asset_load::LoadObjectByPath(w.c_str());
                if (cls) {
                    const std::wstring name = R::ToString(R::NameOf(cls));
                    std::ofstream out(scriptDir + "/" + std::string(name.begin(), name.end()) + "_script.txt",
                                      std::ios::binary | std::ios::trunc);
                    for (const R::ObjectRef& o : R::ChildObjectsOf(cls)) {
                        if (o.className != L"Function") continue;
                        const ue_wrap::script_fingerprint::Result r = ue_wrap::script_fingerprint::Of(o.object);
                        out << "=== " << std::string(o.name.begin(), o.name.end()) << '\n' << r.text << '\n';
                    }
                }
                UE_LOGI("class_dump: script of %ls %hs", w.c_str(), cls ? "written" : "did NOT load");
                d.store(1);
            });
        }
    }
    std::stringstream ss(list);
    std::string one;
    while (std::getline(ss, one, ',')) {
        const std::wstring w(one.begin(), one.end());
        ue_wrap::game_thread::RunAndWait([&w](std::atomic<int>& d) { DumpClass(w); d.store(1); });
    }
    UE_LOGI("class_dump: DONE");
    return 0;
}

}  // namespace harness::autotest
