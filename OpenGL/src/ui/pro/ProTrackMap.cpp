#include "ProTrackMap.h"
#include "../../racing/RaceManager.h"
#include "../../rendering/Interpolation.h"
#include "../../vehicle/Vehicle.h"
#include "../UI_Config.h"
#include <imgui.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cmath>

extern RaceManager* g_race_manager;
extern std::vector<SplinePoint> g_smooth_track_points;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

// Small sector-time callout box drawn directly on the track map
static void DrawSectorCallout(ImDrawList* dl, const ProContext& ctx,
                               ImVec2 anchorSc,  // screen pos of sector start on track
                               const char* sectorLabel, const char* timeStr,
                               ImU32 accentCol, float ux) {
    float bw = 155.f * ux;
    float bh = 34.f  * ux;
    float fSz = ctx.russo ? ctx.russo->FontSize : 11.f;

    // Place box above anchor, clamped to window bounds
    ImVec2 bpos = {anchorSc.x - bw * 0.5f, anchorSc.y - bh - 12.f * ux};

    // Label above box
    dl->AddText(ctx.russo, fSz * 0.88f,
                {bpos.x, bpos.y - fSz - 2.f}, accentCol, sectorLabel);

    // Box BG
    dl->AddRectFilled(bpos, {bpos.x + bw, bpos.y + bh}, IM_COL32(22, 22, 22, 230));
    dl->AddRect      (bpos, {bpos.x + bw, bpos.y + bh}, accentCol, 0.f, 0, 1.f);
    // Colored left accent strip
    dl->AddRectFilled(bpos, {bpos.x + 4.f, bpos.y + bh}, accentCol);

    // Time text
    float fBig = fSz * 1.1f;
    dl->AddText(ctx.regular ? ctx.regular : ctx.russo, fBig,
                {bpos.x + 10.f, bpos.y + (bh - fBig) * 0.5f},
                COL_WHITE, timeStr);

    // Connector line + dot at track point
    dl->AddLine({anchorSc.x, bpos.y + bh}, anchorSc, IM_COL32(80,80,80,200), 1.f);
    dl->AddCircleFilled(anchorSc, 4.f * ux, accentCol);
}

// Bottom status strip: driver info + S1/S2/S3 colored cells + LAP TIME
static void DrawStatusStrip(ImDrawList* dl, const ProContext& ctx,
                             ImVec2 base, float stripW, float stripH,
                             const char* driverNum, const char* driverName,
                             const char* s1, const char* s2, const char* s3,
                             const char* lapTime, float ux) {
    dl->AddRectFilled(base, {base.x + stripW, base.y + stripH}, IM_COL32(16,16,16,255));
    dl->AddLine(base, {base.x + stripW, base.y}, COL_SEP, 1.f);

    float fSz = ctx.russo ? ctx.russo->FontSize : 12.f;
    float cy  = base.y + stripH * 0.5f;
    float x   = base.x + 10.f;

    // Car number (bold, white, large)
    float numW = 28.f * ux;
    dl->AddText(ctx.russo, fSz * 1.3f, {x, cy - fSz * 0.7f}, COL_WHITE, driverNum);
    x += numW;
    // Driver name
    dl->AddText(ctx.russo, fSz, {x, cy - fSz * 0.5f}, COL_DIM, driverName);
    x += ImGui::CalcTextSize(driverName).x + 16.f * ux;

    // Vertical divider
    dl->AddLine({x, base.y + 4.f}, {x, base.y + stripH - 4.f}, COL_SEP, 1.f);
    x += 10.f * ux;

    // Sector cells
    struct SC { const char* label; const char* time; ImU32 col; };
    SC sectors[] = {{"SECTOR 1", s1, COL_S1}, {"SECTOR 2", s2, COL_S2}, {"SECTOR 3", s3, COL_S3}};
    float cellW  = 110.f * ux;
    float cellH  = stripH - 8.f;

    for (auto& sc : sectors) {
        ImVec2 cp = {x, base.y + 4.f};
        dl->AddRectFilled(cp, {cp.x + cellW, cp.y + cellH}, sc.col);
        // Label (tiny, at top of cell)
        dl->AddText(ctx.russo, fSz * 0.75f, {cp.x + 4.f, cp.y + 2.f},
                    IM_COL32(20,20,20,255), sc.label);
        // Time (white, center)
        float timeW = ImGui::CalcTextSize(sc.time).x;
        dl->AddText(ctx.regular ? ctx.regular : ctx.russo, fSz,
                    {cp.x + (cellW - timeW) * 0.5f, cp.y + cellH * 0.4f},
                    IM_COL32(255,255,255,255), sc.time);
        x += cellW + 3.f;
    }

    x += 8.f * ux;
    dl->AddLine({x, base.y + 4.f}, {x, base.y + stripH - 4.f}, COL_SEP, 1.f);
    x += 10.f * ux;

    // LAP TIME label + big time
    dl->AddText(ctx.russo, fSz * 0.85f, {x, cy - fSz}, COL_LABEL, "LAP TIME");
    dl->AddText(ctx.bold  ? ctx.bold : ctx.russo, fSz * 1.2f,
                {x, cy + 1.f}, COL_WHITE, lapTime);
}

void RenderTrackMapWindow(const ProContext& ctx, int32_t vehicleId,
                           ImVec2 vpSz, float topH) {
    ImGui::SetNextWindowPos ({210.f, topH},           ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({970.f, 600.f},           ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({200.f, 150.f}, {vpSz.x, vpSz.y});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG));
    if (!ImGui::Begin("##TrackMap", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    float w  = ImGui::GetWindowWidth();
    float h  = ImGui::GetWindowHeight();
    float ux = w / UIConfig::BASE_WIDTH;
    float uy = h / UIConfig::BASE_HEIGHT;

    DrawPanelHeader(ctx, "TRACK MAP");

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();

    // Strip height at bottom
    const float STRIP_H = fmaxf(40.f * uy, 32.f);
    float mapH = h - HDR_H - 2.f - STRIP_H - 4.f;
    float mapW = w;

    dl->AddRectFilled(base, {base.x + mapW, base.y + mapH}, COL_BG);

    if (g_smooth_track_points.empty()) {
        const char* msg = "No track loaded — drag a .trk2 file here";
        ImVec2 tSz = ImGui::CalcTextSize(msg);
        ImGui::SetCursorScreenPos({base.x + (mapW - tSz.x) * 0.5f,
                                   base.y + (mapH - tSz.y) * 0.5f});
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
        ImGui::TextUnformatted(msg);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos({base.x, base.y + mapH});
        ImGui::Dummy(ImVec2(mapW, 0));
    } else {
        // Compute bounds
        const size_t n = g_smooth_track_points.size();
        glm::vec2 lo = g_smooth_track_points[0].position;
        glm::vec2 hi = lo;
        for (auto& sp : g_smooth_track_points) {
            lo.x = lo.x < sp.position.x ? lo.x : sp.position.x;
            lo.y = lo.y < sp.position.y ? lo.y : sp.position.y;
            hi.x = hi.x > sp.position.x ? hi.x : sp.position.x;
            hi.y = hi.y > sp.position.y ? hi.y : sp.position.y;
        }
        float rX = hi.x - lo.x; if (rX < 1e-6f) rX = 1.f;
        float rY = hi.y - lo.y; if (rY < 1e-6f) rY = 1.f;
        float pad = 32.f;
        float scale = fminf((mapW - pad*2) / rX, (mapH - pad*2) / rY);
        float offX  = base.x + (mapW - rX*scale) * 0.5f;
        float offY  = base.y + (mapH - rY*scale) * 0.5f;

        auto toScreen = [&](glm::vec2 p) -> ImVec2 {
            return {offX + (p.x - lo.x)*scale, offY + (rY - (p.y - lo.y))*scale};
        };

        // Track lines — white/light gray, thick
        const float trackTh = fmaxf(mapH * 0.016f, 3.f);
        const ImU32 trackCol = IM_COL32(170, 170, 170, 255);
        for (size_t i = 1; i < n; ++i) {
            ImVec2 a = toScreen(g_smooth_track_points[i-1].position);
            ImVec2 b = toScreen(g_smooth_track_points[i  ].position);
            dl->AddLine(a, b, trackCol, trackTh);
        }
        if (n > 2) {
            ImVec2 a = toScreen(g_smooth_track_points.back().position);
            ImVec2 b = toScreen(g_smooth_track_points.front().position);
            dl->AddLine(a, b, trackCol, trackTh);
        }

        // Start/finish line
        ImVec2 sf = toScreen(g_smooth_track_points.front().position);
        dl->AddLine({sf.x - 6.f, sf.y}, {sf.x + 6.f, sf.y}, COL_GOLD, 3.f);

        // Sector boundary points (~1/3 and ~2/3 of track)
        ImVec2 s1Pt = toScreen(g_smooth_track_points[n / 3    ].position);
        ImVec2 s2Pt = toScreen(g_smooth_track_points[n * 2 / 3].position);
        ImVec2 s3Pt = sf;

        // Sector callout boxes with placeholder times
        DrawSectorCallout(dl, ctx, s2Pt, "SECTOR 2", "--:--.---", COL_S2, ux);
        DrawSectorCallout(dl, ctx, s1Pt, "SECTOR 1", "--:--.---", COL_S1, ux);
        DrawSectorCallout(dl, ctx, s3Pt, "SECTOR 3", "--:--.---", COL_S3, ux);

        // Vehicle position dot
        double vx = 0, vy = 0; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_vehicles_mutex);
            auto it = g_vehicles.find(vehicleId);
            if (it != g_vehicles.end()) {
                vx = it->second.m_normalized_x;
                vy = it->second.m_normalized_y;
                found = true;
            }
        }
        if (found) {
            ImVec2 dot = toScreen({(float)vx, (float)vy});
            float  dr  = fmaxf(mapH * 0.012f, 5.f);
            dl->AddCircleFilled(dot, dr + 2.f, IM_COL32(220, 40, 40, 60));
            dl->AddCircleFilled(dot, dr,        IM_COL32(225, 165, 0,  255));
            dl->AddCircle      (dot, dr,        IM_COL32(255, 255, 255, 200), 16, 1.f);
        }

        ImGui::SetCursorScreenPos({base.x, base.y + mapH});
        ImGui::Dummy(ImVec2(mapW, 0.f));

        // Bottom status strip
        float curTime = g_race_manager ? g_race_manager->GetVehicleCurrentLapTime(vehicleId) : 0.f;
        char  lapBuf[32];
        fmtTime(curTime, lapBuf, sizeof(lapBuf));

        std::string dname = "---";
        char dnum[8] = "?";
        {
            std::lock_guard<std::mutex> lk(g_vehicles_mutex);
            auto it = g_vehicles.find(vehicleId);
            if (it != g_vehicles.end()) {
                dname = it->second.name;
                snprintf(dnum, sizeof(dnum), "%d", vehicleId);
            }
        }

        ImVec2 stripBase = {base.x, base.y + mapH + 4.f};
        DrawStatusStrip(dl, ctx, stripBase, mapW, STRIP_H,
                        dnum, dname.c_str(),
                        "--:--.---", "--:--.---", "--:--.---", lapBuf, ux);
    }

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg override
}

} // namespace Pro
