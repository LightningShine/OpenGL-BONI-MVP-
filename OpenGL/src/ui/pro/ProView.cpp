#include "ProView.h"
#include "ProLapList.h"
#include "ProChannels.h"
#include "ProSessionInfo.h"
#include "ProGForce.h"
#include "ProGForceBars.h"
#include "ProTrackMap.h"
#include "ProTrackReport.h"
#include "ProGraphs.h"
#include "ProLaptime.h"
#include "ProEvents.h"
#include "ProSectors.h"
#include "ProRelative.h"
#include "../../core/WorldSnapshot.h"
#include "../../network/ReplayPlayer.h"
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
extern int g_focused_vehicle_id;

namespace Pro {

bool g_pro_layout_locked = false;
int  g_layout_freeze_frames = 0;

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

float HeaderButton(const ProContext& ctx, const char* label, bool active, bool arrow,
                   float rightEdge, bool& clicked) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();

    ImFont*     f    = ctx.russo ? ctx.russo : ImGui::GetFont();
    const float fSz  = ui_scale::points(UIConfig::FONT_PT_RUSSO_SMALL);
    const float tw   = f->CalcTextSizeA(fSz, FLT_MAX, 0.f, label).x;
    const float ah   = arrow ? ui_scale::points(4.f) : 0.f;
    const float gap  = arrow ? ui_scale::points(5.f) : 0.f;
    const float full = tw + gap + (arrow ? ah * 2.f : 0.f);
    const float x    = rightEdge - full;
    const float grow = ui_scale::points(4.f);

    const bool hov = ImGui::IsMouseHoveringRect({x - grow, wp.y},
                                                {x + full + grow, wp.y + header_h()}, false);
    const ImU32 col = (active || hov) ? COL_HDR_TEXT : COL_LABEL;
    dl->AddText(f, fSz, {x, wp.y + (header_h() - fSz) * 0.5f}, col, label);

    if (arrow) {
        const float ax = x + tw + gap;
        const float ay = wp.y + header_h() * 0.5f;
        dl->AddTriangleFilled({ax, ay - ah * 0.5f}, {ax + ah * 2.f, ay - ah * 0.5f},
                              {ax + ah, ay + ah * 0.9f}, col);
    }

    clicked = hov && ImGui::IsMouseClicked(0);
    return x;
}

void PushDropdownStyle() {
    const ImVec2 dsz = ImGui::GetIO().DisplaySize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {UIConfig::DROPDOWN_PADDING_X * dsz.x,
                         UIConfig::DROPDOWN_PADDING_Y * dsz.y});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UIConfig::DROPDOWN_BORDER_SIZE);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   UIConfig::DROPDOWN_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        {UIConfig::DROPDOWN_ITEM_SPACING_X * dsz.x,
                         UIConfig::DROPDOWN_ITEM_SPACING_Y * dsz.y});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {UIConfig::DROPDOWN_ITEM_PADDING_X * dsz.x,
                         UIConfig::DROPDOWN_ITEM_PADDING_Y * dsz.y});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(UIConfig::DROPDOWN_BG_R, UIConfig::DROPDOWN_BG_G,
                                                   UIConfig::DROPDOWN_BG_B, UIConfig::DROPDOWN_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(UIConfig::DROPDOWN_BORDER_R, UIConfig::DROPDOWN_BORDER_G,
                                                   UIConfig::DROPDOWN_BORDER_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text,    ImVec4(UIConfig::DROPDOWN_TEXT_R, UIConfig::DROPDOWN_TEXT_G,
                                                   UIConfig::DROPDOWN_TEXT_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Header,  ImVec4(UIConfig::DROPDOWN_HOVER_R, UIConfig::DROPDOWN_HOVER_G,
                                                   UIConfig::DROPDOWN_HOVER_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(UIConfig::DROPDOWN_HOVER_R, UIConfig::DROPDOWN_HOVER_G,
                                                         UIConfig::DROPDOWN_HOVER_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(UIConfig::DROPDOWN_ACTIVE_R, UIConfig::DROPDOWN_ACTIVE_G,
                                                         UIConfig::DROPDOWN_ACTIVE_B, 1.f));
}

void PopDropdownStyle() {
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(5);
}

float SessionTimeSeconds() {
    // На повторе часы сессии показывают, сколько оператор смотрит запись, а не
    // когда событие случилось в заезде: сессия стартует в момент открытия
    // файла. Поэтому берём позицию в записи.
    if (telemetry::replay_is_active())
        return telemetry::replay_status().position_ms / 1000.0f;

    return g_race_manager ? g_race_manager->GetRaceElapsedTime() : 0.f;
}

// ── Круг для разбора (см. ProView.h) ────────────────────────────────────────
static int s_analysis_lap = -1;   // -1 — идти за повтором

int AnalysisLap(int32_t vehicleId) {
    // Ноль — ВЫЕЗДНОЙ круг, он выбирается наравне с остальными; «нет выбора»
    // это -1 (см. RaceConstants::OUT_LAP_NUMBER).
    if (s_analysis_lap >= 0) return s_analysis_lap;
    return g_race_manager ? g_race_manager->GetVehicleCurrentLapNumber(vehicleId) : 0;
}

void SelectAnalysisLap(int lapNumber) { s_analysis_lap = lapNumber; }

// ── Машина, которую разбирают панели ────────────────────────────────────────
// ДЕРЖИТСЯ, ПОКА ОНА НА ТРАССЕ.
//
// Раньше здесь каждый кадр брался ЛИДЕР таблицы. Лидер по ходу заезда меняется
// — а на повторе ещё и на каждой перемотке, потому что откат пересчитывает
// позиции, — и панели молча переезжали на другую машину. На экране это
// выглядело так, будто у одного и того же заезда прыгают времена кругов: щёлк
// по кругу — список показывает 1:34.130 / 1:33.880, щёлк ещё раз — 1:33.221 /
// 1:33.513. Обе пары настоящие, просто от РАЗНЫХ машин.
//
// Выбор машины — дело оператора (боковое меню, клик по машине). Пока он не
// выбрал, берём одну и не отпускаем: переезжать самостоятельно панель разбора
// не имеет права.
static int32_t s_display_vehicle = -1;

static bool vehicleIsOnTrack(int32_t vehicleId) {
    if (vehicleId == -1) return false;
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    return g_vehicles.find(vehicleId) != g_vehicles.end();
}

static int32_t getDisplayVehicleId() {
    if (g_focused_vehicle_id != -1) {
        s_display_vehicle = g_focused_vehicle_id;
        return s_display_vehicle;
    }

    if (vehicleIsOnTrack(s_display_vehicle))
        return s_display_vehicle;

    // Машины не стало (другая запись, начало заезда) — выбираем заново.
    if (g_race_manager) {
        auto standings = g_race_manager->GetStandings();
        if (!standings.empty()) {
            s_display_vehicle = standings.front().vehicleID;
            return s_display_vehicle;
        }
    }
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (!snapshot->vehicles.empty()) {
        s_display_vehicle = snapshot->vehicles.begin()->first;
        return s_display_vehicle;
    }
    s_display_vehicle = -1;
    return -1;
}

void Render(const ProContext& ctx, float swipeAnim) {
    ImGuiViewport* vp   = ImGui::GetMainViewport();
    const ImVec2   sz   = vp->Size;
    const float    topH = UIConfig::top_bar_px();
    const float    botH = UIConfig::bottom_bar_px();

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

    // Выбор круга живёт, только пока запись стоит: тронулась — панели снова
    // идут за повтором. Проверяем ДО отрисовки панелей, иначе выбор, сделанный
    // щелчком в списке, снимался бы в том же кадре, в котором сделан.
    {
        const telemetry::ReplayStatus replay = telemetry::replay_status();
        if (!replay.active || !replay.paused) SelectAnalysisLap(-1);
    }

    // Сменилась машина — выбранный круг к ней не относится: номера кругов у
    // машин свои, и третий круг одной ничего не говорит о третьем круге другой.
    static int32_t s_last_vehicle = -1;
    if (vehicleId != s_last_vehicle) {
        s_last_vehicle = vehicleId;
        SelectAnalysisLap(-1);
    }

    const float panelTopH = topH; // PRO-статус-бар убран — панели идут сразу под верхним меню

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

    // Панели рисуются только если включены в боковом меню (McLaren-style).
    if (PanelVisible("LapList"))     RenderLapListWindow    (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("Channels"))    RenderChannelsWindow   (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("SessionInfo")) RenderSessionInfoWindow(ctx, vehicleId, sz, panelTopH);

    if (PanelVisible("TrackMap"))    RenderTrackMapWindow   (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("TrackReport")) RenderTrackReportWindow(ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("Relative"))    RenderRelativeWindow   (ctx, vehicleId, sz, panelTopH);

    if (PanelVisible("Events"))      RenderEventsWindow     (ctx, sz, panelTopH);
    if (PanelVisible("GForce"))      RenderGForceWindow     (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("GForceLong"))  RenderGForceLongWindow (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("GForceLat"))   RenderGForceLatWindow  (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("Graphs"))      RenderGraphsWindow     (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("Sectors"))     RenderSectorsWindow    (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("Laptime"))     RenderLaptimeWindow    (ctx, vehicleId, sz, panelTopH);

    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(5);

    // Боковое меню групп (правый край) — поверх всего, само управляет видимостью.
    RenderSidebar(ctx, sz, panelTopH, botH);
}

} // namespace Pro
