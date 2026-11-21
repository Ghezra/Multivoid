// coop/dev/menu_spawn_drive.cpp -- see coop/dev/menu_spawn_drive.h.

#include "coop/dev/menu_spawn_drive.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/world/votv_lib.h"
#include "ue_wrap/world/world_singleton.h"

#include <cstdint>
#include <cwctype>

namespace coop::dev::menu_spawn_drive {
namespace {

namespace R = ue_wrap::reflection;

void* Processor() { return R::FindObjectByClass(L"propProcessor_C"); }

void* Field(void* obj, const wchar_t* name) {
    const int32_t off = obj ? R::FindPropertyOffset(R::ClassOf(obj), name) : -1;
    return off < 0 ? nullptr : *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(obj) + off);
}

bool Resolve(void*& widget, void*& fn) {
    widget = Field(Processor(), L"spawnmenu");
    fn = widget && R::IsLive(widget) ? R::FindFunction(R::ClassOf(widget), L"spawn") : nullptr;
    return fn != nullptr;
}

std::vector<std::wstring> Rows() {
    std::vector<std::wstring> out;
    void* const p = Processor();
    const int32_t off = p ? R::FindPropertyOffset(R::ClassOf(p), L"propsNames") : -1;
    if (off < 0) return out;
    const auto* view = reinterpret_cast<const ue_wrap::field_io::TArrayView*>(static_cast<const uint8_t*>(p) + off);
    if (!view->data || view->num <= 0) return out;
    const auto* names = reinterpret_cast<const R::FName*>(view->data);
    for (int32_t i = 0; i < view->num; ++i) out.push_back(R::ToString(names[i]));
    return out;
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

}  // namespace

bool Ready() {
    void* w = nullptr;
    void* f = nullptr;
    return Resolve(w, f);
}

bool CatalogHas(const std::wstring& row) {
    for (const std::wstring& r : Rows())
        if (r == row) return true;
    return false;
}

std::vector<std::wstring> RowsContaining(const std::wstring& part, size_t cap) {
    std::vector<std::wstring> out;
    const std::wstring p = Lower(part);
    for (const std::wstring& r : Rows()) {
        if (out.size() >= cap) break;
        if (Lower(r).find(p) != std::wstring::npos) out.push_back(r);
    }
    return out;
}

void* RowClass(const std::wstring& row) {
    void* const gm = ue_wrap::world_singleton::Gamemode();
    ue_wrap::votv_lib::PropObject obj;
    if (!gm || !ue_wrap::votv_lib::PropToObject(ue_wrap::fname_utils::StringToFName(row), gm, obj)) return nullptr;
    return obj.objectClass;
}

bool Spawn(const std::wstring& row) {
    void* widget = nullptr;
    void* fn = nullptr;
    if (!Resolve(widget, fn)) return false;
    const R::FName name = ue_wrap::fname_utils::StringToFName(row);
    ue_wrap::ParamFrame f(fn);
    return f.valid() && f.SetRaw(L"name", &name, sizeof(name)) && ue_wrap::Call(widget, f);
}

}  // namespace coop::dev::menu_spawn_drive
