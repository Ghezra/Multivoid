// ue_wrap/world/whiteboard.cpp -- see ue_wrap/world/whiteboard.h.

#include "ue_wrap/world/whiteboard.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/script_fingerprint.h"

#include <cstring>

namespace ue_wrap::whiteboard {
namespace {

namespace OI = ue_wrap::object_index;
namespace R  = ue_wrap::reflection;

int32_t g_offSurface = -1;    // prop_whiteboard_C.whiteboardobj
int32_t g_offRt = -1;         // whiteboard_C.rt
int32_t g_offActive = -1;     // whiteboard_C.Active
void*   g_toString = nullptr;      // whiteboard_C.TextureToString
void*   g_toTexture = nullptr;     // whiteboard_C.stringToTexture
std::wstring g_pictureParam;  // stringToTexture's one parameter
std::wstring g_pictureOut;    // TextureToString's returned text field ('String')
bool    g_resolved = false, g_failed = false;

bool Resolve() {
    if (g_resolved) return true;
    if (g_failed) return false;
    void* const propCls = OI::ClassByName(L"prop_whiteboard_C");
    void* const surfCls = OI::ClassByName(L"whiteboard_C");
    if (!propCls || !surfCls) return false;   // the world has not loaded them yet
    g_offSurface = R::FindPropertyOffset(propCls, L"whiteboardobj");
    g_offRt = R::FindPropertyOffset(surfCls, L"rt");
    g_offActive = R::FindPropertyOffset(surfCls, L"Active");
    g_toString = R::FindFunction(surfCls, L"TextureToString");
    g_toTexture = R::FindFunction(surfCls, L"stringToTexture");
    if (g_toTexture) {
        for (const R::StructFieldInfo& f : R::EnumerateStructFields(g_toTexture))
            if (g_pictureParam.empty()) g_pictureParam = f.name;
    }
    if (g_offSurface < 0 || g_offRt < 0 || !g_toString || !g_toTexture || g_pictureParam.empty()) {
        g_failed = true;
        UE_LOGW("whiteboard: resolution INCOMPLETE (surface=0x%X rt=0x%X toString=%s toTexture=%s param='%ls') -- the "
                "board is not synced",
                g_offSurface, g_offRt, g_toString ? "yes" : "NO", g_toTexture ? "yes" : "NO", g_pictureParam.c_str());
        return false;
    }
    if (g_toString) {
        // The returned text is a field of the function like its locals: the ubergraph's temporaries are all named
        // CallFunc_*, so the one that is not is the text the function returns.
        for (const R::StructFieldInfo& f : R::EnumerateStructFields(g_toString)) {
            UE_LOGI("whiteboard: TextureToString field '%ls' size=%d", f.name.c_str(), f.size);
            if (g_pictureOut.empty() && f.name.rfind(L"CallFunc_", 0) != 0) g_pictureOut = f.name;
        }
    }
    g_resolved = true;
    UE_LOGI("whiteboard: resolved (surface=0x%X rt=0x%X toString=yes toTexture=yes param='%ls')", g_offSurface, g_offRt,
            g_pictureParam.c_str());
    return true;
}

std::string Narrow(const std::wstring& s) {
    std::string out;
    out.reserve(s.size());
    for (wchar_t c : s) out.push_back((c > 0 && c < 128) ? static_cast<char>(c) : '?');
    return out;
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace

void FindBoards(std::vector<void*>& out, size_t max) {
    out.clear();
    std::vector<void*> found = R::FindObjectsByClass(L"prop_whiteboard_C");
    for (void* prop : found) {
        if (out.size() >= max) break;
        out.push_back(prop);
    }
}

bool IsBoard(void* prop) {
    if (!prop || !R::IsLive(prop)) return false;
    return R::ToString(R::NameOf(R::ClassOf(prop))) == L"prop_whiteboard_C";
}

void* SurfaceOf(void* prop) {
    if (!Resolve() || !prop || !R::IsLive(prop)) return nullptr;
    void* surface = nullptr;
    std::memcpy(&surface, static_cast<const uint8_t*>(prop) + g_offSurface, sizeof(surface));
    return surface && R::IsLive(surface) ? surface : nullptr;
}

bool CanRead(void* surface) {
    if (!Resolve() || !surface || !R::IsLive(surface)) return false;
    void* rt = nullptr;
    std::memcpy(&rt, static_cast<const uint8_t*>(surface) + g_offRt, sizeof(rt));
    return rt != nullptr;
}

bool IsActive(void* surface) {
    if (!Resolve() || !surface || !R::IsLive(surface) || g_offActive < 0) return false;
    return static_cast<const uint8_t*>(surface)[g_offActive] != 0;
}

std::string PictureOf(void* surface) {
    if (!CanRead(surface)) return {};
    // The function resolved on whiteboard_C, the class that declares it: an instance's own class may be a Blueprint
    // child of it, and asking that child for a function it inherits by name misses.
    void* const fn = g_toString;
    if (!fn) return {};
    ue_wrap::ParamFrame f(fn);
    if (!f.valid()) { UE_LOGW("whiteboard: TextureToString's frame is invalid"); return {}; }
    if (!ue_wrap::Call(surface, f)) { UE_LOGW("whiteboard: TextureToString did not run"); return {}; }
    const int32_t off = f.ParamOffset(g_pictureOut.empty() ? L"String" : g_pictureOut.c_str());
    if (off < 0) { UE_LOGW("whiteboard: TextureToString has no ReturnValue parameter at this offset"); return {}; }
    const std::wstring text = ue_wrap::field_io::ReadFStringAt(f.data(), off);
    if (text.empty()) { UE_LOGW("whiteboard: TextureToString returned an empty string (frame off=0x%X)", off); return {}; }
    return Narrow(text);
}

bool ApplyPicture(void* surface, const std::string& picture) {
    if (!Resolve() || !surface || !R::IsLive(surface) || picture.empty()) return false;
    void* const fn = g_toTexture;
    if (!fn) return false;
    ue_wrap::ParamFrame f(fn);
    if (!f.valid()) return false;
    const int32_t off = f.ParamOffset(g_pictureParam.c_str());
    if (off < 0) return false;
    if (!ue_wrap::field_io::WriteFStringField(f.data(), off, Widen(picture))) return false;
    return ue_wrap::Call(surface, f);
}

uint64_t KeyOf(void* prop) {
    if (!prop || !R::IsLive(prop)) return 0;
    // The prop's own sync key first: a mirror carries the same Aprop_Key as the prop it copies, while a full object
    // path differs between the two peers. The path is the fallback for a prop whose key has not been stamped yet.
    std::string id = Narrow(ue_wrap::prop::GetKeyString(prop));
    if (id.empty()) id = ue_wrap::script_fingerprint::PathOf(prop);
    uint64_t h = 1469598103934665603ull;
    for (char c : id) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace ue_wrap::whiteboard