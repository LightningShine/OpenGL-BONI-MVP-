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

// ── Palette (from the Time.svg mockup) ──────────────────────────────────────
static constexpr ImU32 LT_LABEL = IM_COL32(0xA4, 0xA3, 0xA3, 255); // #A4A3A3
static constexpr ImU32 LT_VALUE = IM_COL32(0xF0, 0xF0, 0xF0, 255); // #F0F0F0
static constexpr ImU32 LT_GOLD  = IM_COL32(0xDA, 0xA9, 0x40, 255); // #DAA940 solid

void RenderLaptimeWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({1380.f * ui, topH + 600.f * ui}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({240.f * ui,  243.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({170.f * ui, 120.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Laptime", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float       w  = ImGui::GetWindowWidth();
    float       z  = PanelZoom("Laptime");
    DrawPanelHeader(ctx, "LAPTIME", true, nullptr, z); // gear icon
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float lblSz = (ctx.regular ? ctx.regular->FontSize : 12.f) * z;
    float valSz = (ctx.bold    ? ctx.bold->FontSize    : 16.f) * z;
    float ttlSz = (ctx.title   ? ctx.title->FontSize   : 32.f) * z;
    float pad   = pad_px() * z;
    float valX  = pad + (w - 2.f * pad) * 0.42f; // left edge of the value column

    // Solid gold separator at the current cursor, with padding above/below.
    auto goldLine = [&]() {
        ImGui::Dummy(ImVec2(0, 7.f * z));
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine({p.x + pad, p.y}, {p.x + w - pad, p.y}, LT_GOLD, 1.f);
        ImGui::Dummy(ImVec2(0, 9.f * z));
    };

    // Label (gray, regular) + value (white, bold) left-aligned at valX. The value
    // is shifted left if it would otherwise overflow the right padding.
    auto row = [&](const char* lbl, const char* val, ImU32 vcol) {
        float  rowH = valSz + 4.f * z;
        ImVec2 p    = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.regular, lblSz, {p.x + pad, p.y + (rowH - lblSz) * 0.5f}, LT_LABEL, lbl);

        float vw = ctx.bold ? ctx.bold->CalcTextSizeA(valSz, FLT_MAX, 0.f, val).x
                            : ImGui::CalcTextSize(val).x;
        float vx = p.x + valX;
        float maxX = p.x + w - pad - vw;
        if (vx > maxX) vx = maxX;
        dl->AddText(ctx.bold, valSz, {vx, p.y + (rowH - valSz) * 0.5f}, vcol, val);

        ImGui::Dummy(ImVec2(w, rowH + 3.f * z));
    };

    // ── Big LAPTIME ──────────────────────────────────────────────────────────
    float curTime = g_race_manager ? g_race_manager->GetVehicleCurrentLapTime(vehicleId) : 0.f;
    char  tb[32];
    fmtTime(curTime, tb, sizeof(tb));

    ImGui::Dummy(ImVec2(0, 10.f * z));
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.title, ttlSz, {p.x + pad, p.y}, LT_VALUE, tb);
        ImGui::Dummy(ImVec2(w, ttlSz + 4.f * z));
    }

    goldLine();

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
    row("SPEED", vb, LT_VALUE);
    snprintf(vb, sizeof(vb), "%.2f m/s\xc2\xb2", accel);
    row("ACCELE.", vb, LT_VALUE);

    goldLine();

    // ── TIME DIFF ────────────────────────────────────────────────────────────
    float delta = g_race_manager ? g_race_manager->GetVehicleLapDelta(vehicleId) : 0.f;
    char  db[32];
    fmtDelta(delta, db, sizeof(db));
    ImU32 dCol = delta < 0.f ? COL_GREEN : (delta > 0.f ? COL_RED : LT_LABEL);
    row("TIME DIFF", db, dCol);

    ImGui::End();
}

} // namespace Pro
