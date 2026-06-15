#include "ProSectors.h"
#include "../../rendering/Interpolation.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <vector>
#include <cmath>
#include <cstdio>

extern std::vector<SplinePoint> g_smooth_track_points;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

// Map curvature (0=tight corner → slow, 1=straight → fast) to a SLOW→FAST gradient
static ImU32 speedColor(float t) {
    // t: 0=slow (red), 1=fast (green). Uses simple red→yellow→green ramp.
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    float r, g, b;
    if (t < 0.5f) { r = 1.f;     g = t * 2.f; b = 0.f; }
    else          { r = 1.f - (t - 0.5f) * 2.f; g = 1.f; b = 0.f; }
    return IM_COL32((int)(r * 220), (int)(g * 210), 30, 255);
}

void RenderSectorsWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    ImGui::SetNextWindowPos ({635.f, topH + 600.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({550.f, 225.f},         ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f, 100.f}, {vpSz.x, vpSz.y});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG_WIDGET));
    if (!ImGui::Begin("##Sectors", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    DrawPanelHeader(ctx, "SECTORS");

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();

    // Reserved for SLOW/FAST labels at bottom
    const float labelH = 18.f;
    float mapW = w;
    float mapH = h - HDR_H - 2.f - labelH;

    dl->AddRectFilled(base, {base.x + mapW, base.y + mapH}, COL_BG);

    if (g_smooth_track_points.empty()) {
        const char* msg = "No track";
        ImVec2 tSz = ImGui::CalcTextSize(msg);
        dl->AddText(nullptr, 0.f,
                    {base.x + (mapW - tSz.x)*0.5f, base.y + (mapH - tSz.y)*0.5f},
                    IM_COL32(60,60,60,255), msg);
    } else {
        const size_t n = g_smooth_track_points.size();

        // Bounds
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
        float pad   = 20.f;
        float scale = fminf((mapW - pad*2) / rX, (mapH - pad*2) / rY);
        float offX  = base.x + (mapW - rX*scale) * 0.5f;
        float offY  = base.y + (mapH - rY*scale) * 0.5f;

        auto toScreen = [&](glm::vec2 p) -> ImVec2 {
            return {offX + (p.x - lo.x)*scale, offY + (rY - (p.y - lo.y))*scale};
        };

        // Compute per-segment curvature and map to speed color
        // curvature: dot product of consecutive unit segments (1=straight, -1=hairpin)
        const float trackTh = fmaxf(mapH * 0.022f, 4.f);

        for (size_t i = 1; i + 1 < n; ++i) {
            glm::vec2 a = g_smooth_track_points[i-1].position;
            glm::vec2 b = g_smooth_track_points[i  ].position;
            glm::vec2 c = g_smooth_track_points[i+1].position;

            glm::vec2 ab = b - a; float abL = sqrtf(ab.x*ab.x + ab.y*ab.y);
            glm::vec2 bc = c - b; float bcL = sqrtf(bc.x*bc.x + bc.y*bc.y);
            float curvature = 0.5f; // straight
            if (abL > 1e-6f && bcL > 1e-6f) {
                float dot = (ab.x/abL)*(bc.x/bcL) + (ab.y/abL)*(bc.y/bcL);
                // dot: 1=straight, -1=hairpin → map to 0..1
                curvature = (dot + 1.f) * 0.5f;
            }

            ImVec2 pa = toScreen(a);
            ImVec2 pb = toScreen(b);
            dl->AddLine(pa, pb, speedColor(curvature), trackTh);
        }
        // Close loop
        if (n > 2) {
            ImVec2 a = toScreen(g_smooth_track_points[n-2].position);
            ImVec2 b = toScreen(g_smooth_track_points[n-1].position);
            dl->AddLine(a, b, speedColor(0.5f), trackTh);
            a = toScreen(g_smooth_track_points[n-1].position);
            b = toScreen(g_smooth_track_points[0].position);
            dl->AddLine(a, b, speedColor(0.5f), trackTh);
        }

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
            float  dr  = fmaxf(mapH * 0.025f, 4.f);
            dl->AddCircleFilled(dot, dr,     IM_COL32(240, 240, 240, 255));
            dl->AddCircle      (dot, dr + 2, IM_COL32(60, 60, 60, 200), 16, 1.f);
        }
    }

    // SLOW / FAST gradient legend bar
    float barY  = base.y + mapH + 4.f;
    float barH  = labelH - 4.f;
    float barX0 = base.x + PAD;
    float barX1 = base.x + mapW - PAD;
    int   steps = 120;
    for (int i = 0; i < steps; ++i) {
        float t0 = (float)i       / steps;
        float t1 = (float)(i + 1) / steps;
        ImVec2 p0 = {barX0 + t0 * (barX1 - barX0), barY};
        ImVec2 p1 = {barX0 + t1 * (barX1 - barX0), barY + barH};
        dl->AddRectFilled(p0, p1, speedColor(t0));
    }
    float fSz = ctx.russo ? ctx.russo->FontSize : 11.f;
    dl->AddText(ctx.russo, fSz * 0.85f, {barX0, barY - fSz - 1.f}, COL_DIM, "SLOW");
    float fastW = ImGui::CalcTextSize("FAST").x;
    dl->AddText(ctx.russo, fSz * 0.85f,
                {barX1 - fastW, barY - fSz - 1.f}, COL_DIM, "FAST");

    ImGui::SetCursorScreenPos({base.x, base.y + mapH + labelH});
    ImGui::Dummy(ImVec2(mapW, 0.f));

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

} // namespace Pro
