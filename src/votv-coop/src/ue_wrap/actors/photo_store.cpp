// ue_wrap/actors/photo_store.cpp -- see ue_wrap/actors/photo_store.h.

#include "ue_wrap/actors/photo_store.h"

#include "ue_wrap/actors/inventory.h"    // ResolveSaveSlot
#include "ue_wrap/actors/save_record.h"  // ReadArr, ReplaceArray, AllocZeroed, WriteArrHeader
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"

#include <cstring>

namespace ue_wrap::photo_store {
namespace {

namespace R  = ue_wrap::reflection;
namespace SR = ue_wrap::save_record;

constexpr int32_t kEntry = 16;         // Fstruct_byteImage: one TArray<uint8> header
constexpr int32_t kMaxIndex = 100000;  // a gallery past this is not a save's

struct Arrays {
    void* save = nullptr;
    int32_t photos = -1, phys = -1;
};

Arrays Resolve() {
    Arrays a;
    a.save = ue_wrap::inventory::ResolveSaveSlot();
    if (!a.save) return a;
    void* const cls = R::ClassOf(a.save);
    static void* s_cls = nullptr;
    static int32_t s_photos = -1, s_phys = -1;
    if (cls != s_cls) {
        s_cls = cls;
        s_photos = R::FindPropertyOffset(cls, L"photos");
        s_phys = R::FindPropertyOffset(cls, L"photos_phys");
        if (s_photos < 0 || s_phys < 0)
            UE_LOGE("photo_store: the save object's photos (%d) or photos_phys (%d) did not resolve", s_photos, s_phys);
    }
    a.photos = s_photos;
    a.phys = s_phys;
    return a;
}

// A copy of the outer array's entry headers, `n` of them at least, the new ones empty.
std::vector<uint8_t> Headers(const SR::Arr& outer, int32_t n) {
    std::vector<uint8_t> h(static_cast<size_t>(n) * kEntry, 0);
    if (outer.num > 0) std::memcpy(h.data(), outer.data, static_cast<size_t>(outer.num) * kEntry);
    return h;
}

}  // namespace

int32_t Count(bool phys) {
    const Arrays a = Resolve();
    const int32_t off = phys ? a.phys : a.photos;
    return a.save && off >= 0 ? SR::ReadArr(a.save, off).num : -1;
}

bool Read(bool phys, int32_t index, std::vector<uint8_t>& out) {
    out.clear();
    const Arrays a = Resolve();
    const int32_t off = phys ? a.phys : a.photos;
    if (!a.save || off < 0 || index < 0) return false;
    const SR::Arr outer = SR::ReadArr(a.save, off);
    if (index >= outer.num) return false;
    const SR::Arr inner = SR::ReadArr(outer.data + static_cast<size_t>(index) * kEntry, 0);
    if (inner.num <= 0) return false;
    out.assign(inner.data, inner.data + inner.num);
    return true;
}

int32_t AppendGallery(const std::vector<uint8_t>& bytes) {
    const Arrays a = Resolve();
    if (!a.save || a.photos < 0 || bytes.empty()) return -1;
    const SR::Arr outer = SR::ReadArr(a.save, a.photos);
    if (outer.num >= kMaxIndex) return -1;
    void* const buf = SR::AllocZeroed(bytes.size(), 1);
    if (!buf) return -1;
    std::memcpy(buf, bytes.data(), bytes.size());
    std::vector<uint8_t> h = Headers(outer, outer.num + 1);
    SR::WriteArrHeader(h.data() + static_cast<size_t>(outer.num) * kEntry, 0, buf, static_cast<int32_t>(bytes.size()));
    if (!SR::ReplaceArray(a.save, a.photos, h.data(), outer.num + 1, kEntry)) {
        R::EngineFree(buf);
        return -1;
    }
    return outer.num;
}

bool WritePhys(int32_t index, const std::vector<uint8_t>& bytes) {
    const Arrays a = Resolve();
    if (!a.save || a.phys < 0 || index < 0 || index >= kMaxIndex || bytes.empty()) return false;
    const SR::Arr outer = SR::ReadArr(a.save, a.phys);
    const int32_t n = index < outer.num ? outer.num : index + 1;
    void* const buf = SR::AllocZeroed(bytes.size(), 1);
    if (!buf) return false;
    std::memcpy(buf, bytes.data(), bytes.size());
    std::vector<uint8_t> h = Headers(outer, n);
    const SR::Arr old = SR::ReadArr(h.data() + static_cast<size_t>(index) * kEntry, 0);
    SR::WriteArrHeader(h.data() + static_cast<size_t>(index) * kEntry, 0, buf, static_cast<int32_t>(bytes.size()));
    if (!SR::ReplaceArray(a.save, a.phys, h.data(), n, kEntry)) {
        R::EngineFree(buf);
        return false;
    }
    if (old.num > 0 && old.data) R::EngineFree(const_cast<uint8_t*>(old.data));
    return true;
}

}  // namespace ue_wrap::photo_store
