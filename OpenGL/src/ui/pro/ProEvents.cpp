#include "ProEvents.h"
#include <imgui.h>
#include <cstdio>

namespace Pro {

struct RaceEvent {
    const char* time;
    const char* text;
    ImU32       col;
};

static const RaceEvent s_events[] = {
    { "01:02:41", "RACE: Race start in 15:35 local time", IM_COL32(200,200,200,255) },
    { "00:56:22", "RACE: Race stopped by steward",         IM_COL32(255, 75, 75, 255) },
    { "00:40:21", "Yellow flags for S3",                   IM_COL32(218,165, 64, 255) },
    { "00:38:52", "Best S1 - CAR 22",                      IM_COL32(  0,210,110, 255) },
    { "00:37:49", "Best Lap CAR 4",                        IM_COL32(  0,210,110, 255) },
    { "00:37:49", "Best S2 - Personal Best",               IM_COL32(  0,188,255, 255) },
    { "00:37:18", "Lap Improved 1:14:532 -> 1:14:089",     IM_COL32(  0,210,110, 255) },
};

void RenderEventsWindow(const ProContext& ctx, ImVec2 vpSz, float topH) {
    ImGui::SetNextWindowPos ({210.f, topH + 600.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({200.f, 225.f},         ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({120.f, 80.f}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Events", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    DrawPanelHeader(ctx, "EVENTS");

    float scrollH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##evScroll", {w, scrollH}, false);

    constexpr int N = (int)(sizeof(s_events) / sizeof(s_events[0]));
    float fSz = ctx.russo ? ctx.russo->FontSize : ImGui::GetFontSize();
    float fReg = ctx.regular ? ctx.regular->FontSize : ImGui::GetFontSize();

    ImDrawList* dl  = ImGui::GetWindowDrawList();

    for (int i = 0; i < N; ++i) {
        const RaceEvent& ev = s_events[i];
        ImVec2 p = ImGui::GetCursorScreenPos();

        // Timestamp (Russo, label color)
        dl->AddText(ctx.russo, fSz, {p.x + PAD, p.y + 1.f}, IM_COL32(0x51,0x51,0x51,255), ev.time);
        // Event text (Ubuntu regular, colored)
        float textY = p.y + fSz + 2.f;
        dl->AddText(ctx.regular, fReg, {p.x + PAD, textY}, ev.col, ev.text);

        float rowH = fSz + fReg + 6.f;
        ImGui::Dummy(ImVec2(w, rowH));

        // Separator (except last)
        if (i < N - 1) {
            ImVec2 sp = ImGui::GetCursorScreenPos();
            dl->AddLine({sp.x, sp.y}, {sp.x + w, sp.y}, COL_SEP, 1.f);
            ImGui::Dummy(ImVec2(w, 1.f));
        }
    }

    // Auto-scroll to bottom (newest events at top in real impl, reversed here for demo)
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.f);

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
