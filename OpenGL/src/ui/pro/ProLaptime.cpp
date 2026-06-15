#include "ProLaptime.h"
#include "../../racing/RaceManager.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <cstdio>
#include <cmath>

extern RaceManager* g_race_manager;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

void RenderLaptimeWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    ImGui::SetNextWindowPos ({1380.f, topH + 600.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({220.f,  225.f},         ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({160.f, 120.f}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Laptime", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    DrawPanelHeader(ctx, "LAPTIME", true); // gear icon

    ImDrawList* dl  = ImGui::GetWindowDrawList();

    auto separator = [&]() {
        ImGui::Dummy(ImVec2(0, 6.f));
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine({p.x + PAD, p.y}, {p.x + w - PAD, p.y}, COL_GOLD_DIM, 1.f);
        ImGui::Dummy(ImVec2(0, 6.f));
    };

    // ── Big LAPTIME ──────────────────────────────────────────────────────────
    float curTime = g_race_manager ? g_race_manager->GetVehicleCurrentLapTime(vehicleId) : 0.f;
    char  tb[32];
    fmtTime(curTime, tb, sizeof(tb));

    ImGui::SetCursorPosX(PAD);
    if (ctx.title) ImGui::PushFont(ctx.title);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_WHITE));
    ImGui::TextUnformatted(tb);
    ImGui::PopStyleColor();
    if (ctx.title) ImGui::PopFont();

    separator();

    // ── SPEED / ACCELE. ──────────────────────────────────────────────────────
    double speed = 0, accel = 0;
    {
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            speed = it->second.m_speed_kph;
            accel = it->second.m_acceleration;
        }
    }

    char vb[32];
    snprintf(vb, sizeof(vb), "%.1f Km/h", speed);
    LabelValue(ctx, "SPEED", vb);
    ImGui::Dummy(ImVec2(0, 4.f));
    snprintf(vb, sizeof(vb), "%.2f m/s\xc2\xb2", accel);
    LabelValue(ctx, "ACCELE.", vb);

    separator();

    // ── TIME DIFF ────────────────────────────────────────────────────────────
    float delta = g_race_manager ? g_race_manager->GetVehicleLapDelta(vehicleId) : 0.f;
    char  db[32];
    fmtDelta(delta, db, sizeof(db));
    ImU32 dCol = delta < 0.f ? COL_GREEN : (delta > 0.f ? COL_RED : COL_DIM);
    LabelValue(ctx, "TIME DIFF", db, dCol);

    ImGui::End();
}

} // namespace Pro
