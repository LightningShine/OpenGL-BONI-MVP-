#include "ProChannels.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <cstdio>

extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

void RenderChannelsWindow(const ProContext& ctx, int32_t vehicleId,
                           ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({0.f,   topH + 310.f * ui},      ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210.f * ui, 240.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f * ui, 80.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Channels", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float z = PanelZoom("Channels");
    DrawPanelHeader(ctx, "CHANELS", false, nullptr, z);
    ImGui::SetWindowFontScale(z);

    // Live GPS data
    double speed = 0, gx = 0, gy = 0, accel = 0, progress = 0;
    int16_t fixType = 0;
    {
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            const Vehicle& v = it->second;
            speed = v.m_speed_kph; gx = v.m_g_force_x; gy = v.m_g_force_y;
            accel = v.m_acceleration; fixType = v.m_fix_type;
            progress = v.m_track_progress;
        }
    }
    const char* fixLabel = fixType == 5 ? "RTK Fixed" :
                           fixType == 4 ? "RTK Float" :
                           fixType >= 1 ? "GPS"       : "No Fix";
    ImU32 fixCol = fixType == 5 ? COL_GREEN : fixType >= 1 ? COL_GOLD : COL_RED;

    char vb[24];

    // Column header
    const float colIdR   = w * 0.12f;
    const float colNameX = w * 0.17f;
    const float colValX  = w * 0.56f;

    auto colHdr = [&](const char* s, float x, bool right) {
        if (right) {
            ImGui::SetCursorPosX(x - ImGui::CalcTextSize(s).x);
        } else {
            ImGui::SetCursorPosX(x);
        }
        if (ctx.russo) ImGui::PushFont(ctx.russo);
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
        if (ctx.russo) ImGui::PopFont();
    };
    colHdr("ID", colIdR, true);
    ImGui::SameLine(colNameX); colHdr("NAME", colNameX, false);
    ImGui::SameLine(colValX);  colHdr("VALUE", colValX, false);
    DrawSep();

    // Channels: first 8 = future CAN placeholders, rest = live GPS
    struct Ch { int id; const char* name; const char* val; ImU32 valCol; };

    snprintf(vb, sizeof(vb), "%.1f km/h", speed); char vSpeed[24]; snprintf(vSpeed, 24, "%s", vb);
    char vGLong[24]; snprintf(vGLong, 24, "%.2f g", gy);
    char vGLat[24];  snprintf(vGLat,  24, "%.2f g", gx);
    char vAccel[24]; snprintf(vAccel, 24, "%.2f m/s\xc2\xb2", accel);
    char vProg[24];  snprintf(vProg,  24, "%.1f %%", progress * 100.0);

    const Ch channels[] = {
        {  0, "RPM",       "9158 rpm",  COL_DIM   }, // CAN placeholder
        {  1, "Gear",      "3",         COL_DIM   },
        {  2, "Throttle",  "100 %",     COL_DIM   },
        {  3, "Brake",     "0 %",       COL_DIM   },
        {  4, "Steering",  "-4 deg",    COL_DIM   },
        {  5, "Oil Temp",  "92 C",      COL_DIM   },
        {  6, "H2O Temp",  "78 C",      COL_DIM   },
        {  7, "Fuel",      "55 l",      COL_DIM   },
        {  8, "Speed",     vSpeed,      COL_WHITE  },
        {  9, "gForce Lg", vGLong,      COL_WHITE  },
        { 10, "gForce Lt", vGLat,       COL_WHITE  },
        { 11, "Accel",     vAccel,      COL_WHITE  },
        { 12, "GPS Fix",   fixLabel,    fixCol     },
        { 13, "Lap Prog",  vProg,       COL_WHITE  },
    };

    float scrollH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##chanScroll", {w, scrollH}, false);
    ImGui::SetWindowFontScale(z);

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2      wp  = ImGui::GetWindowPos();
    char        id[8];
    const float eyeX = w - pad_px() - 4.f;

    for (auto& ch : channels) {
        snprintf(id, sizeof(id), "%d", ch.id);

        // ID (right-aligned, Russo, label color)
        {
            float tw = ImGui::CalcTextSize(id).x;
            ImGui::SetCursorPosX(colIdR - tw);
        }
        if (ctx.russo) ImGui::PushFont(ctx.russo);
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
        ImGui::TextUnformatted(id);
        ImGui::PopStyleColor();

        // Name — clipped so it doesn't overflow into value column
        ImGui::SameLine(colNameX);
        ImVec2 clipMin = {wp.x + colNameX - ImGui::GetScrollX(), wp.y};
        ImVec2 clipMax = {wp.x + colValX - 6.f,                  wp.y + 9999.f};
        ImGui::PushClipRect(clipMin, clipMax, true);
        ImGui::TextUnformatted(ch.name);
        ImGui::PopClipRect();
        if (ctx.russo) ImGui::PopFont();

        // Value (Ubuntu)
        ImGui::SameLine(colValX);
        if (ctx.regular) ImGui::PushFont(ctx.regular);
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(ch.valCol));
        ImGui::TextUnformatted(ch.val);
        ImGui::PopStyleColor();
        if (ctx.regular) ImGui::PopFont();

        // Decorative eye icon
        {
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            float cy = (rmin.y + rmax.y) * 0.5f;
            float cx = wp.x - ImGui::GetScrollX() + eyeX;
            dl->AddCircle   ({cx, cy}, 5.f,  COL_DIM, 12, 1.f);
            dl->AddCircleFilled({cx, cy}, 2.f, COL_DIM);
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
