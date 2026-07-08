#include "ProLapInfo.h"
#include "../../racing/RaceManager.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <cstdio>
#include <ctime>
#include <string>

extern RaceManager* g_race_manager;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

// Sector row: label | time (right mid) | delta (right edge)
static void SectorRow(const ProContext& ctx, float w,
                       const char* lbl, const char* time,
                       const char* delta, ImU32 timeCol, ImU32 deltaCol) {
    ImGui::SetCursorPosX(PAD);
    if (ctx.russo) ImGui::PushFont(ctx.russo);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
    ImGui::TextUnformatted(lbl);
    ImGui::PopStyleColor();
    if (ctx.russo) ImGui::PopFont();

    if (ctx.regular) ImGui::PushFont(ctx.regular);
    // Time: right-aligned at 68% of width
    float midX = w * 0.68f;
    float tw   = ImGui::CalcTextSize(time).x;
    ImGui::SameLine(midX - tw);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(timeCol));
    ImGui::TextUnformatted(time);
    ImGui::PopStyleColor();
    // Delta: right-aligned at right edge
    float dw = ImGui::CalcTextSize(delta).x;
    ImGui::SameLine(w - PAD - dw);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(deltaCol));
    ImGui::TextUnformatted(delta);
    ImGui::PopStyleColor();
    if (ctx.regular) ImGui::PopFont();
}

void RenderLapInfoWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    ImGui::SetNextWindowPos ({0.f,   topH + 550.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210.f, 160.f},         ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f, 80.f}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##LapInfo", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float z = PanelZoom("LapInfo");
    DrawPanelHeader(ctx, "LAP INFO", false, nullptr, z);
    ImGui::SetWindowFontScale(z);

    if (!g_race_manager) { ImGui::SetWindowFontScale(1.f); ImGui::End(); return; }

    int   curLap   = g_race_manager->GetVehicleCurrentLapNumber(vehicleId);
    float prevTime = g_race_manager->GetVehiclePreviousLapTime(vehicleId);
    float delta    = g_race_manager->GetVehicleLapDelta(vehicleId);
    int   pos      = 0;
    {
        auto standings = g_race_manager->GetStandings();
        for (auto& s : standings)
            if (s.vehicleID == vehicleId) { pos = s.position; break; }
    }

    char tb[32], db[32], pb[16];
    snprintf(tb, sizeof(tb), "%d", curLap);
    LabelValue(ctx, "Lap",      tb);
    if (pos > 0) snprintf(pb, sizeof(pb), "P%d", pos);
    else         snprintf(pb, sizeof(pb), "--/--");
    LabelValue(ctx, "Position", pb);

    ImGui::Dummy(ImVec2(0, 2.f));

    SectorRow(ctx, w, "S1", "--:--.---", "---", COL_LABEL, COL_LABEL);
    SectorRow(ctx, w, "S2", "--:--.---", "---", COL_LABEL, COL_LABEL);
    SectorRow(ctx, w, "S3", "--:--.---", "---", COL_LABEL, COL_LABEL);

    ImGui::Dummy(ImVec2(0, 2.f));

    fmtTime(prevTime, tb, sizeof(tb));
    fmtDelta(delta, db, sizeof(db));
    ImU32 dCol = delta < 0.f ? COL_GREEN : (delta > 0.f ? COL_RED : COL_DIM);
    SectorRow(ctx, w, "Last", tb, db, COL_WHITE, dCol);

    ImGui::End();
}

void RenderSessionInfoWindow(const ProContext& ctx, int32_t vehicleId,
                              ImVec2 vpSz, float topH) {
    ImGui::SetNextWindowPos ({0.f,   topH + 710.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210.f, 140.f},         ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f, 80.f}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##SessionInfo", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float z = PanelZoom("SessionInfo");
    DrawPanelHeader(ctx, "SESSION INFO", false, nullptr, z);
    ImGui::SetWindowFontScale(z);

    std::string driverName = "---";
    {
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) driverName = it->second.name;
    }

    char dateBuf[32] = "---";
    std::time_t t = std::time(nullptr);
    std::tm tm_info{};
    if (localtime_s(&tm_info, &t) == 0)
        strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &tm_info);

    char elapsed[32] = "--:--";
    if (g_race_manager) {
        float e = g_race_manager->GetRaceElapsedTime();
        if (e > 0.f) {
            int m = (int)(e / 60.f), s = (int)(e) % 60;
            snprintf(elapsed, sizeof(elapsed), "%02d:%02d", m, s);
        }
    }

    LabelValue(ctx, "Driver",   driverName.c_str());
    LabelValue(ctx, "Engineer", "---");
    LabelValue(ctx, "Circuit",  "---");
    LabelValue(ctx, "Vehicle",  std::to_string(vehicleId).c_str());
    LabelValue(ctx, "Session",  elapsed);
    LabelValue(ctx, "Date",     dateBuf);

    ImGui::End();
}

} // namespace Pro
