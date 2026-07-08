#include "ProView.h"
#include "ProLapList.h"
#include "ProChannels.h"
#include "ProLapInfo.h"
#include "ProGForce.h"
#include "ProTrackMap.h"
#include "ProLaptime.h"
#include "ProEvents.h"
#include "ProSectors.h"
#include "../../vehicle/Vehicle.h"
#include "../../racing/RaceManager.h"
#include "../../racing/StopReset/StartStop.h"
#include "../UI_Config.h"
#include <imgui.h>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <fstream>

extern RaceManager* g_race_manager;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;
extern int g_focused_vehicle_id;

namespace Pro {

bool g_pro_layout_locked = false;

// ── Per-panel text zoom (persisted to pro_scales.ini) ───────────────────────
static std::unordered_map<std::string, float> g_panelScale;
static bool g_scalesLoaded = false;
static const char* kScaleFile = "pro_scales.ini";

static void loadScales() {
    if (g_scalesLoaded) return;
    g_scalesLoaded = true;
    std::ifstream f(kScaleFile);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        float v = (float)atof(line.c_str() + eq + 1);
        if (v > 0.3f && v < 5.f) g_panelScale[line.substr(0, eq)] = v;
    }
}

static void saveScales() {
    std::ofstream f(kScaleFile, std::ios::trunc);
    if (!f) return;
    for (auto& [k, v] : g_panelScale) f << k << "=" << v << "\n";
}

float PanelZoom(const char* key) {
    loadScales();
    auto it = g_panelScale.find(key);
    float sc = (it != g_panelScale.end()) ? it->second : 1.f;

    ImGuiIO& io = ImGui::GetIO();
    bool changed = false;
    if (ImGui::IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.f) {
        sc += io.MouseWheel * 0.08f; changed = true;
    }
    if (ImGui::IsWindowFocused() && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))      { sc += 0.1f; changed = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) { sc -= 0.1f; changed = true; }
    }
    if (changed) {
        if (sc < 0.6f) sc = 0.6f; if (sc > 2.5f) sc = 2.5f;
        g_panelScale[key] = sc;
        saveScales();
    }
    return sc;
}

static int32_t getDisplayVehicleId() {
    if (g_focused_vehicle_id != -1) return g_focused_vehicle_id;
    if (g_race_manager) {
        auto standings = g_race_manager->GetStandings();
        if (!standings.empty()) return standings.front().vehicleID;
    }
    std::lock_guard<std::mutex> lk(g_vehicles_mutex);
    if (!g_vehicles.empty()) return g_vehicles.begin()->first;
    return -1;
}

void Render(const ProContext& ctx, float swipeAnim) {
    ImGuiViewport* vp   = ImGui::GetMainViewport();
    const ImVec2   sz   = vp->Size;
    const float    topH = UIConfig::TOP_MENU_HEIGHT    * sz.y;
    const float    botH = UIConfig::BOTTOM_MENU_HEIGHT * sz.y;

    // Swipe-in animation — solid dark panel slides from left, panels hidden
    if (swipeAnim < 0.99f) {
        float panelX = vp->Pos.x + sz.x * (swipeAnim - 1.f);
        ImGui::SetNextWindowPos({panelX, vp->Pos.y + topH});
        ImGui::SetNextWindowSize({sz.x, sz.y - topH - botH});
        ImGui::SetNextWindowBgAlpha(1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG));
        ImGui::Begin("##ProAnim", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize       |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar    |
            ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    int32_t vehicleId = getDisplayVehicleId();
    static constexpr float STATUS_H = 30.f;
    const float panelTopH = topH + STATUS_H;

    // ── Global style for all floating panels ─────────────────────────────────
    // NOTE: the app-wide style sets a display-scaled ItemSpacing (huge: thousands
    // of px). Every other UI region overrides it; the pro panels do precise manual
    // layout (DrawList + explicit Dummy gaps) and need it neutralised, otherwise
    // each item/Dummy advances the cursor far below the window and all content is
    // clipped away. Force tight spacing here.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,             (ImVec4)ImColor(COL_BG_PANEL));
    ImGui::PushStyleColor(ImGuiCol_Border,               (ImVec4)ImColor(COL_SEP));
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip,           (ImVec4)ImColor(COL_GOLD_DIM));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered,    (ImVec4)ImColor(COL_GOLD));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,     (ImVec4)ImColor(COL_GOLD));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(18, 18, 18, 255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        IM_COL32(55, 55, 55, 255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(80, 80, 80, 255));

    // Left sidebar
    RenderLapListWindow    (ctx, vehicleId, sz, panelTopH);
    RenderChannelsWindow   (ctx, vehicleId, sz, panelTopH);
    RenderLapInfoWindow    (ctx, vehicleId, sz, panelTopH);
    RenderSessionInfoWindow(ctx, vehicleId, sz, panelTopH);

    // Center track view
    RenderTrackMapWindow   (ctx, vehicleId, sz, panelTopH);

    // Bottom cards
    RenderEventsWindow     (ctx, sz, panelTopH);
    RenderGForceWindow     (ctx, vehicleId, sz, panelTopH);
    RenderSectorsWindow    (ctx, vehicleId, sz, panelTopH);
    RenderLaptimeWindow    (ctx, vehicleId, sz, panelTopH);

    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(5);

    // ── PRO Race Status Bar (rendered last so it always sits on top) ──────────
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 14, 14, 255));
    ImGui::SetNextWindowPos ({vp->Pos.x, vp->Pos.y + topH}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({sz.x, STATUS_H},               ImGuiCond_Always);
    ImGui::Begin("##ProStatusBar", nullptr,
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize       |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar    |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        ImVec2      bp  = ImGui::GetWindowPos();
        float       fSz = ctx.russo ? ctx.russo->FontSize : ImGui::GetFontSize();
        float       ty  = bp.y + (STATUS_H - fSz) * 0.5f;
        float       cy  = bp.y + STATUS_H * 0.5f;

        dl->AddLine({bp.x, bp.y + STATUS_H - 1.f},
                    {bp.x + sz.x, bp.y + STATUS_H - 1.f}, COL_SEP, 1.f);

        // ── Left: session state ───────────────────────────────────────────
        SessionState ss = g_race_manager
                        ? g_race_manager->GetSessionState() : SessionState::Idle;
        bool  racing    = (ss == SessionState::Active || ss == SessionState::Finishing);
        ImU32 dotCol    = racing ? COL_GREEN : ss == SessionState::Ended ? COL_GOLD : COL_DIM;
        const char* stateStr = racing ? "RACE" : ss == SessionState::Ended ? "ENDED" : "STANDBY";
        dl->AddCircleFilled({bp.x + 14.f, cy}, 4.f, dotCol);
        dl->AddText(ctx.russo, fSz, {bp.x + 25.f, ty}, dotCol, stateStr);

        // ── Center: LAP  |  TIME ─────────────────────────────────────────
        char lapBuf[16]  = "LAP --";
        char timeBuf[12] = "--:--";
        if (g_race_manager) {
            int   lap     = g_race_manager->GetVehicleCurrentLapNumber(vehicleId);
            float elapsed = g_race_manager->GetRaceElapsedTime();
            snprintf(lapBuf,  sizeof(lapBuf),  "LAP %d", lap > 0 ? lap : 1);
            int m = (int)(elapsed / 60.f), s = (int)elapsed % 60;
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", m, s);
        }
        auto txtW = [&](const char* t) -> float {
            return ctx.russo ? ctx.russo->CalcTextSizeA(fSz, FLT_MAX, 0.f, t).x
                             : ImGui::CalcTextSize(t).x;
        };
        const char* sep  = "  |  ";
        float lapW  = txtW(lapBuf), sepW = txtW(sep), timeW = txtW(timeBuf);
        float cx    = bp.x + (sz.x - lapW - sepW - timeW) * 0.5f;
        dl->AddText(ctx.russo, fSz, {cx,               ty}, COL_TEXT, lapBuf);
        dl->AddText(ctx.russo, fSz, {cx + lapW,        ty}, COL_DIM,  sep);
        dl->AddText(ctx.russo, fSz, {cx + lapW + sepW, ty}, COL_TEXT, timeBuf);

        // ── Right: best lap ───────────────────────────────────────────────
        char bestBuf[32] = "BEST  --:--.---";
        if (g_race_manager) {
            float best = g_race_manager->GetVehicleBestLapTime(vehicleId);
            if (best > 0.f) {
                char bt[16]; fmtTime(best, bt, sizeof(bt));
                snprintf(bestBuf, sizeof(bestBuf), "BEST  %s", bt);
            }
        }
        dl->AddText(ctx.russo, fSz, {bp.x + sz.x - txtW(bestBuf) - 14.f, ty}, COL_DIM, bestBuf);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace Pro
