#include "ProGForce.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <cmath>
#include <cstdio>

extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

void RenderGForceWindow(const ProContext& ctx, int32_t vehicleId,
                         ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({410.f * ui, topH + 600.f * ui}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({225.f * ui, 225.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({120.f * ui, 120.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##GForce", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    float z = PanelZoom("GForce");
    DrawPanelHeader(ctx, "G-FORCE", false, nullptr, z);

    // Live data
    double gx = 0, gy = 0;
    {
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            gx = it->second.m_g_force_x;
            gy = it->second.m_g_force_y;
        }
    }

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();
    float       sz   = ImGui::GetContentRegionAvail().y; // remaining height → square

    // Background
    dl->AddRectFilled(base, {base.x + sz, base.y + sz}, COL_BG_WIDGET);

    float cx = base.x + sz * 0.5f;
    float cy = base.y + sz * 0.5f;
    float r  = sz * 0.40f;

    // 4G range labels (top corners via draw list)
    float fSz = (ctx.russo ? ctx.russo->FontSize : ImGui::GetFontSize()) * z;
    char maxBuf[24];
    snprintf(maxBuf, sizeof(maxBuf), "4G (%.1f)", fabsf((float)gy));
    dl->AddText(ctx.russo, fSz, {base.x + 4.f, base.y + 4.f},
                IM_COL32(90,90,90,255), maxBuf);
    snprintf(maxBuf, sizeof(maxBuf), "4G (%.1f)", fabsf((float)gx));
    ImVec2 latSz = ImGui::CalcTextSize(maxBuf);
    dl->AddText(ctx.russo, fSz, {base.x + sz - latSz.x - 4.f, base.y + sz - fSz - 4.f},
                IM_COL32(90,90,90,255), maxBuf);

    // Grid: concentric circles + crosshair
    const ImU32 gridCol = IM_COL32(55, 55, 55, 255);
    for (int i = 1; i <= 4; ++i)
        dl->AddCircle({cx, cy}, r * i * 0.25f, gridCol, 64, 1.f);
    dl->AddLine({cx - r, cy}, {cx + r, cy}, gridCol, 1.f);
    dl->AddLine({cx, cy - r}, {cx, cy + r}, gridCol, 1.f);

    // G-force dot
    float maxG = 4.f;
    float dotX = cx + ((float)gx / maxG) * r;
    float dotY = cy - ((float)gy / maxG) * r;
    float dotR = fmaxf(sz * 0.024f, 4.f);
    dl->AddCircleFilled({dotX, dotY}, dotR + 3.f, IM_COL32(180, 40, 40, 50));
    dl->AddCircleFilled({dotX, dotY}, dotR,        IM_COL32(220, 50, 50, 255));
    dl->AddCircle      ({dotX, dotY}, dotR,        IM_COL32(255, 120, 120, 180));

    // Advance cursor so window resizing works
    ImGui::SetCursorScreenPos({base.x, base.y + sz});
    ImGui::Dummy(ImVec2(sz, 0.f));

    ImGui::End();
}

} // namespace Pro
