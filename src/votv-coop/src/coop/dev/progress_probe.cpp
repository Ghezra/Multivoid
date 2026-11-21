// coop/dev/progress_probe.cpp -- [dev] a timed tool's progress bar on slot 1, for the frame rate. See the header.

#include "coop/dev/progress_probe.h"

#include "coop/config/config.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace coop::dev::progress_probe {
namespace {

namespace R = ue_wrap::reflection;
using ue_wrap::ParamFrame;

uint64_t Now() { return ::GetTickCount64(); }

bool On() {
    static const bool v = coop::config::ReadEnv("VOTVCOOP_PROGRESS_PROBE") == "1";
    return v;
}

uint64_t g_startAt = 0;
int g_phase = 0;  // 0 waiting, 1 showing, 2 done
void* g_widget = nullptr;
void* g_bar = nullptr;
void* g_setPercent = nullptr;
uint32_t g_frames = 0;

bool CallNoArgs(void* obj, const wchar_t* fn) {
    void* const f = obj ? R::FindFunction(R::ClassOf(obj), fn) : nullptr;
    if (!f) return false;
    ParamFrame p(f);
    return p.valid() && ue_wrap::Call(obj, p);
}

void Show() {
    void* const cls = ue_wrap::object_index::ClassByName(L"ui_linearProgress_C");
    void* const wbl = R::FindClassDefaultObject(L"WidgetBlueprintLibrary");
    void* const create = wbl ? R::FindFunction(R::ClassOf(wbl), L"Create") : nullptr;
    void* const me = coop::players::Registry::Get().Local();
    if (!cls || !create || !me) {
        UE_LOGW("[PROGRESS] cannot show: class=%p create=%p player=%p", cls, create, me);
        return;
    }
    ParamFrame f(create);
    void* noPlayer = nullptr;
    if (!f.valid() || !f.Set(L"WorldContextObject", me) || !f.Set(L"WidgetType", cls) ||
        !f.Set(L"OwningPlayer", noPlayer) || !ue_wrap::Call(wbl, f)) {
        UE_LOGW("[PROGRESS] the widget's Create failed");
        return;
    }
    g_widget = f.Get<void*>(L"ReturnValue");
    if (!g_widget) { UE_LOGW("[PROGRESS] Create returned none"); return; }
    void* const add = R::FindFunction(R::ClassOf(g_widget), L"AddToViewport");
    if (add) {
        ParamFrame a(add);
        const int32_t z = -1;
        if (a.valid()) { a.Set(L"ZOrder", z); ue_wrap::Call(g_widget, a); }
    }
    const int32_t off = R::FindPropertyOffset(R::ClassOf(g_widget), L"pbar");
    if (off >= 0) std::memcpy(&g_bar, static_cast<uint8_t*>(g_widget) + off, sizeof(void*));
    g_setPercent = g_bar ? R::FindFunction(R::ClassOf(g_bar), L"SetPercent") : nullptr;
    UE_LOGI("[PROGRESS] SHOWN widget=%p bar=%p setPercent=%p", g_widget, g_bar, g_setPercent);
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!On() || !session || !session->running() || !session->connected()) return;
    if (session->role() == coop::net::Role::Host || coop::players::Registry::Get().LocalPeerId() != 1) return;
    if (!g_startAt) { g_startAt = Now(); UE_LOGI("[PROGRESS] probe armed"); }
    const uint64_t t = Now() - g_startAt;
    if (g_phase == 0 && t >= 60000u) { g_phase = 1; Show(); }
    if (g_phase == 1) {
        if (t >= 90000u || !g_widget || !R::IsLive(g_widget)) {
            g_phase = 2;
            if (g_widget && R::IsLive(g_widget)) CallNoArgs(g_widget, L"RemoveFromParent");
            UE_LOGI("[PROGRESS] REMOVED after %u frame(s) of SetPercent", g_frames);
            return;
        }
        if (g_bar && g_setPercent && R::IsLive(g_bar)) {
            ParamFrame p(g_setPercent);
            const float pct = static_cast<float>((t - 60000u) % 10000u) / 10000.f;
            if (p.valid() && p.Set(L"InPercent", pct)) ue_wrap::Call(g_bar, p);
            ++g_frames;
        }
    }
}

}  // namespace coop::dev::progress_probe
