// ue_wrap/world/radio.cpp -- see ue_wrap/world/radio.h.

#include "ue_wrap/world/radio.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflected_offset.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"

#include <cstring>
#include <unordered_map>

namespace ue_wrap::radio {
namespace {

namespace R = reflection;

constexpr int64_t kSeekSlackTicks = 20000000;  // 2 s: a peer's own decoder drifts less, and a seek is audible
constexpr int32_t kMaxCatalog = 10000;

std::unordered_map<void*, bool> g_isRadio;

R::InstanceOffset g_data{L"data"};
R::InstanceOffset g_player{L"mediaPlayer"};
R::InstanceOffset g_gamemode{L"gamemode"};
R::InstanceOffset g_mode{L"playmode"};
R::InstanceOffset g_pause{L"pausePlayer"};

void* g_layoutClass = nullptr;
int32_t g_stride = 0, g_link = -1, g_isUrl = -1;

void* Object(void* owner, R::InstanceOffset& field) {
    const int32_t off = owner ? field.Of(owner) : -1;
    if (off < 0) return nullptr;
    void* value = nullptr;
    std::memcpy(&value, static_cast<const uint8_t*>(owner) + off, sizeof(value));
    return value && R::IsLive(value) ? value : nullptr;
}

// The struct_tv row's layout, read off the radio's own `data` and its openLink parameter.
bool Layout(void* actor) {
    void* const cls = R::ClassOf(actor);
    if (!cls) return false;
    if (cls == g_layoutClass) return g_stride > 0;
    g_layoutClass = cls;
    void* const type = R::PropertyInnerStruct(cls, L"data");
    void* const open = R::FindDispatchFunctionCached(cls, L"openLink");
    g_stride = type && open ? R::FindParamSize(open, L"data") : 0;
    g_link = type ? R::FindPropertyOffsetByPrefix(type, L"link_") : -1;
    g_isUrl = type ? R::FindPropertyOffsetByPrefix(type, L"isUrl_") : -1;
    if (g_stride <= 0 || g_link < 0 || g_isUrl < 0) {
        g_stride = 0;
        UE_LOGW("radio: the struct_tv layout did not resolve -- radios stay on each peer");
    }
    return g_stride > 0;
}

uint64_t RowTrack(const uint8_t* row) {
    const std::wstring link = field_io::ReadFStringAt(row, g_link);
    return link.empty() ? 0 : TrackId(link);
}

// Every catalog row the radio can open: the gamemode's two lists.
template <class Fn>
bool ForEachRow(void* actor, Fn&& fn) {
    void* const gamemode = Object(actor, g_gamemode);
    if (!gamemode || !Layout(actor)) return false;
    static R::InstanceOffset sOnline{L"radioLinks"}, sOffline{L"radioLinks_offlin"};
    for (R::InstanceOffset* list : {&sOffline, &sOnline}) {
        const int32_t off = list->Of(gamemode);
        if (off < 0) continue;
        field_io::TArrayView rows{};
        std::memcpy(&rows, static_cast<const uint8_t*>(gamemode) + off, sizeof(rows));
        if (!rows.data || rows.num <= 0 || rows.num > kMaxCatalog || rows.max < rows.num) continue;
        for (int32_t i = 0; i < rows.num; ++i)
            if (fn(rows.data + i * g_stride)) return true;
    }
    return false;
}

template <class T>
bool Query(void* object, const wchar_t* name, T& value) {
    void* const fn = object ? R::FindDispatchFunctionCached(R::ClassOf(object), name) : nullptr;
    if (!fn) return false;
    ParamFrame f(fn);
    return f.valid() && Call(object, f) && f.GetRaw(L"ReturnValue", &value, sizeof(value));
}

bool Command(void* object, const wchar_t* name) {
    bool result = false;
    return Query(object, name, result) && result;
}

// The rule openLink tests, read without the hint its own test shows.
bool CustomContentAllowed(void* actor) {
    void* const lib = R::FindClassDefaultObject(L"lib_C");
    void* const fn = lib ? R::FindDispatchFunctionCached(R::ClassOf(lib), L"isCustom") : nullptr;
    if (!fn) return false;
    ParamFrame f(fn);
    return f.valid() && f.Set<void*>(L"__WorldContext", actor) && Call(lib, f) && f.Get<bool>(L"isCustom");
}

bool Seek(void* player, int64_t ticks) {
    void* const fn = R::FindDispatchFunctionCached(R::ClassOf(player), L"Seek");
    if (!fn) return false;
    ParamFrame f(fn);
    return f.valid() && f.Set<int64_t>(L"Time", ticks) && Call(player, f);
}

}  // namespace

bool IsRadio(void* actor) {
    if (!actor || !R::IsLive(actor)) return false;
    void* const cls = R::ClassOf(actor);
    if (!cls) return false;
    auto it = g_isRadio.find(cls);
    if (it != g_isRadio.end()) return it->second;
    bool radio = false;
    for (void* c = cls; c; c = R::SuperStructOf(c)) {
        if (R::NameEquals(R::NameOf(c), L"prop_radio_atv_C")) {
            radio = false;
            break;
        }
        if (R::NameEquals(R::NameOf(c), kClass)) radio = true;
    }
    g_isRadio.emplace(cls, radio);
    return radio;
}

uint64_t TrackId(std::wstring link) {
    for (wchar_t& c : link) {
        if (c == L'\\') c = L'/';
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + (L'a' - L'A'));
    }
    if (link.find(L"://") == std::wstring::npos || link.rfind(L"file://", 0) == 0) {
        const size_t root = link.rfind(L"/radio/");
        if (root != std::wstring::npos) link.erase(0, root + 7);
        else if (link.rfind(L"radio/", 0) == 0) link.erase(0, 6);
        else if (const size_t slash = link.find_last_of(L'/'); slash != std::wstring::npos) link.erase(0, slash + 1);
    }
    uint64_t hash = 14695981039346656037ull;  // FNV-1a
    for (wchar_t c : link)
        for (unsigned shift : {0u, 8u}) {
            hash ^= (static_cast<uint16_t>(c) >> shift) & 255u;
            hash *= 1099511628211ull;
        }
    return hash ? hash : 1;
}

bool Read(void* actor, State& out) {
    if (!IsRadio(actor) || !Layout(actor)) return false;
    void* const player = Object(actor, g_player);
    const int32_t data = g_data.Of(actor), mode = g_mode.Of(actor);
    if (!player || data < 0 || mode < 0) return false;
    bool preparing = false, playing = false;
    if (!Query(player, L"IsPreparing", preparing) || preparing) return false;
    State value;
    value.track = RowTrack(static_cast<const uint8_t*>(actor) + data);
    value.mode = static_cast<const uint8_t*>(actor)[mode];
    if (value.track) {
        if (!Query(player, L"IsPlaying", playing) || !Query(player, L"GetTime", value.ticks)) return false;
        value.playing = playing;
        if (value.ticks < 0) value.ticks = 0;
    }
    out = value;
    return true;
}

Apply ApplyState(void* actor, const State& state) {
    if (!IsRadio(actor) || !Layout(actor)) return Apply::Waiting;
    void* const player = Object(actor, g_player);
    const int32_t data = g_data.Of(actor), mode = g_mode.Of(actor), pause = g_pause.Of(actor);
    if (!player || data < 0 || mode < 0 || pause < 0) return Apply::Waiting;
    auto* base = static_cast<uint8_t*>(actor);
    base[mode] = state.mode;
    if (!state.track) return Apply::Done;
    bool preparing = false, playing = false;
    if (!Query(player, L"IsPreparing", preparing) || preparing) return Apply::Waiting;
    base[pause] = state.playing ? 0 : 1;  // its opened callback reads this
    if (RowTrack(base + data) != state.track) {
        const uint8_t* found = nullptr;
        if (CustomContentAllowed(actor)) ForEachRow(actor, [&](const uint8_t* row) {
            if (RowTrack(row) != state.track) return false;
            found = row;
            return true;
        });
        if (!found) {
            if (Query(player, L"IsPlaying", playing) && playing) Command(player, L"Pause");
            return Apply::NoSuchTrack;
        }
        void* const open = R::FindDispatchFunctionCached(R::ClassOf(actor), L"openLink");
        if (open) {
            ParamFrame f(open);
            if (f.valid() && f.SetRaw(L"data", found, g_stride) && f.Set<bool>(L"pausePlayer", !state.playing)) Call(actor, f);
        }
        return Apply::Waiting;
    }
    int64_t time = 0;
    if (!Query(player, L"IsPlaying", playing) || !Query(player, L"GetTime", time)) return Apply::Waiting;
    if (time - state.ticks > kSeekSlackTicks || state.ticks - time > kSeekSlackTicks) Seek(player, state.ticks);
    if (playing != state.playing) Command(player, state.playing ? L"Play" : L"Pause");
    return Apply::Done;
}

uint64_t CatalogTrack(void* actor, int index) {
    uint64_t track = 0;
    int at = 0;
    ForEachRow(actor, [&](const uint8_t* row) {
        if (at++ != index) return false;
        track = RowTrack(row);
        return true;
    });
    return track;
}

void* RadioWithOpenInterface(void* player) {
    const int32_t off = player ? reflected_offset::MainPlayer_activeInterface() : -1;
    if (off < 0) return nullptr;
    void* widget = nullptr;
    std::memcpy(&widget, static_cast<const uint8_t*>(player) + off, sizeof(widget));
    if (!widget || !R::IsLive(widget) || !R::NameEquals(R::NameOf(R::ClassOf(widget)), L"ui_radioInterface_C")) return nullptr;
    static R::InstanceOffset sRadio{L"radio"};
    void* const radio = Object(widget, sRadio);
    return IsRadio(radio) ? radio : nullptr;
}

}  // namespace ue_wrap::radio
