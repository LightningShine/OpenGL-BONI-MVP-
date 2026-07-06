#include "ProLapList.h"
#include "../../racing/RaceManager.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <cmath>
#include <cstdio>

extern RaceManager* g_race_manager;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

static int s_selected_lap = -1;

// ── Palette ───────────────────────────────────────────────────────────────────
static constexpr ImU32 LL_BG_SEL    = IM_COL32(0x29, 0x29, 0x29, 255);
static constexpr ImU32 LL_BG_HOVER  = IM_COL32(0x1A, 0x1A, 0x1A, 255);
static constexpr ImU32 LL_ACCENT    = IM_COL32(0xDA, 0xA5, 0x40, 255); // #DAA540
static constexpr ImU32 LL_LAP_NUM   = IM_COL32(0x51, 0x51, 0x51, 255); // #515151
static constexpr ImU32 LL_TIME      = IM_COL32(0xA4, 0xA3, 0xA3, 255); // #A4A3A3
static constexpr ImU32 LL_GAP       = IM_COL32(0xB3, 0xB3, 0xB3, 255); // #B3B3B3
static constexpr ImU32 LL_HDR_COL   = IM_COL32(0xA4, 0xA3, 0xA3, 200); // column headers
static constexpr ImU32 LL_DIM       = IM_COL32(0x64, 0x64, 0x64, 255); // #646464
static constexpr ImU32 LL_FASTEST   = IM_COL32(0xDA, 0xA5, 0x40, 255);
static constexpr ImU32 LL_BEST_TIME = IM_COL32(0xFF, 0xFF, 0xFF, 255);

// ── Layout ────────────────────────────────────────────────────────────────────
static constexpr float ACCENT_W = 3.f;
static constexpr float PAD_L    = 10.f;
static constexpr float LAP_W    = 22.f;   // lap-number column width (right-aligned)
static constexpr float COL_GAP  = 24.f;   // spacing between LAP and TIME columns
static constexpr float PAD_R    = 12.f;
// Design width: columns are pinned to this so shrinking the window past it clips
// content on the right instead of reflowing/squeezing the columns inward.
static constexpr float REF_W    = 200.f;

void RenderLapListWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    ImGui::SetNextWindowPos ({0.f,   topH},          ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210.f, 380.f},          ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({150.f, 100.f}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##LapList", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float z = PanelZoom("LapList");

    // Panel title — Ubuntu Bold
    DrawPanelHeader(ctx, "LAP LIST", false, ctx.bold, z);

    float regSz   = (ctx.regular ? ctx.regular->FontSize : ImGui::GetFontSize()) * z;
    float russoSz = (ctx.russo   ? ctx.russo->FontSize   : ImGui::GetFontSize()) * z;
    float ROW_H   = regSz + 7.f * z;

    // Zoom-scaled layout metrics.
    float padL = PAD_L * z, lapW = LAP_W * z, colGap = COL_GAP * z;
    float padR = PAD_R * z, refW = REF_W * z, accentW = ACCENT_W * z;

    // Pin columns to the design width: when the window is wider, GAP follows the
    // right edge; when narrower, columns hold their position and clip on the right.
    float layoutW = (w > refW) ? w : refW;
    float TIME_X  = padL + lapW + colGap;
    float GAP_R   = layoutW - padR;

    // ── Column headers — drawn to PARENT window draw list (before BeginChild) ─
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p  = ImGui::GetCursorScreenPos();
        float  ty = p.y + 3.f * z;

        // LAP header — left-aligned at the padding edge so it is never clipped.
        dl->AddText(ctx.russo, russoSz, {p.x + padL, ty},   LL_HDR_COL, "LAP");
        dl->AddText(ctx.russo, russoSz, {p.x + TIME_X, ty}, LL_HDR_COL, "TIME");

        float gapW = ctx.russo
            ? ctx.russo->CalcTextSizeA(russoSz, FLT_MAX, 0.f, "GAP").x
            : ImGui::CalcTextSize("GAP").x;
        dl->AddText(ctx.russo, russoSz, {p.x + GAP_R - gapW, ty}, LL_HDR_COL, "GAP");

        ImGui::Dummy({w, russoSz + 5.f * z});
    }
    DrawSep();

    // ── Scrollable rows ───────────────────────────────────────────────────────
    float scrollH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##lapScroll", {w, scrollH}, false);

    // Thread-safe snapshot — the network thread mutates the live lap map, so we
    // copy under the lock rather than iterating a borrowed pointer.
    std::map<int, LapData> laps;
    float bestTime = -1.f;
    int   curLap   = 1;
    float curTime  = 0.f;
    if (g_race_manager) {
        laps     = g_race_manager->GetVehicleLapsCopy(vehicleId);
        bestTime = g_race_manager->GetVehicleBestLapTime(vehicleId);
        curLap   = g_race_manager->GetVehicleCurrentLapNumber(vehicleId);
        curTime  = g_race_manager->GetVehicleCurrentLapTime(vehicleId);
    }

    // ── Row draw helper ───────────────────────────────────────────────────────
    // IMPORTANT: always call ImGui::GetWindowDrawList() INSIDE the lambda so
    // we draw to the child window's draw list, not the parent's.
    auto drawRow = [&](int lapNum, const char* timeStr, const char* gapStr,
                       ImU32 timeCol, ImU32 gapCol, bool isCurrent) {
        ImVec2     p    = ImGui::GetCursorScreenPos();
        ImDrawList* dl  = ImGui::GetWindowDrawList(); // child's draw list
        bool isSel  = (s_selected_lap == lapNum);
        bool active = isCurrent || isSel;
        bool hov    = !active &&
                      ImGui::IsMouseHoveringRect(p, {p.x + w, p.y + ROW_H}) &&
                      !ImGui::IsAnyItemActive();

        if (active)
            dl->AddRectFilled(p, {p.x + w, p.y + ROW_H}, LL_BG_SEL);
        else if (hov)
            dl->AddRectFilled(p, {p.x + w, p.y + ROW_H}, LL_BG_HOVER);

        if (active)
            dl->AddRectFilled(p, {p.x + accentW, p.y + ROW_H}, LL_ACCENT);

        float cy = p.y + (ROW_H - russoSz) * 0.5f;
        float ty = p.y + (ROW_H - regSz)   * 0.5f;

        // Lap number — Russo One, right-aligned
        char nb[8]; snprintf(nb, sizeof(nb), "%d", lapNum);
        float numW = ctx.russo
            ? ctx.russo->CalcTextSizeA(russoSz, FLT_MAX, 0.f, nb).x
            : ImGui::CalcTextSize(nb).x;
        dl->AddText(ctx.russo, russoSz, {p.x + padL + lapW - numW, cy},
                    active ? LL_ACCENT : LL_LAP_NUM, nb);

        // Time — Ubuntu Regular
        dl->AddText(ctx.regular, regSz, {p.x + TIME_X, ty}, timeCol, timeStr);

        // Gap — Ubuntu Regular, right-aligned
        float gW = ctx.regular
            ? ctx.regular->CalcTextSizeA(regSz, FLT_MAX, 0.f, gapStr).x
            : ImGui::CalcTextSize(gapStr).x;
        dl->AddText(ctx.regular, regSz, {p.x + GAP_R - gW, ty}, gapCol, gapStr);

        // Click-to-select
        ImGui::PushID(lapNum);
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##r", {w, ROW_H});
        if (ImGui::IsItemClicked())
            s_selected_lap = (s_selected_lap == lapNum) ? -1 : lapNum;
        ImGui::PopID();
    };

    char tb[32], gb[32];
    bool hasLaps = !laps.empty();

    // ── Out-lap placeholder ───────────────────────────────────────────────────
    if (!hasLaps && curTime <= 0.f) {
        ImVec2     p  = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList(); // child's draw list
        float ty = p.y + (ROW_H - regSz)   * 0.5f;
        float cy = p.y + (ROW_H - russoSz) * 0.5f;

        float nw = ctx.russo
            ? ctx.russo->CalcTextSizeA(russoSz, FLT_MAX, 0.f, "1").x : 10.f;
        dl->AddText(ctx.russo,   russoSz, {p.x + padL + lapW - nw, cy}, LL_LAP_NUM, "1");
        dl->AddText(ctx.regular, regSz,   {p.x + TIME_X, ty},              LL_DIM,      "NO TIME");

        float ow = ctx.regular
            ? ctx.regular->CalcTextSizeA(regSz, FLT_MAX, 0.f, "OUT LAP").x : 50.f;
        dl->AddText(ctx.regular, regSz, {p.x + GAP_R - ow, ty}, LL_DIM, "OUT LAP");
        ImGui::Dummy({w, ROW_H});
    }

    // ── Completed laps ────────────────────────────────────────────────────────
    for (auto& [lapNum, data] : laps) {
        bool isBest = bestTime > 0.f && data.lapTime > 0.f &&
                      fabsf(data.lapTime - bestTime) < 0.001f;

        fmtTime(data.lapTime, tb, sizeof(tb));

        if (isBest)      snprintf(gb, sizeof(gb), "Fastest");
        else if (bestTime > 0.f && data.lapTime > 0.f)
                         fmtDelta(data.lapTime - bestTime, gb, sizeof(gb));
        else             snprintf(gb, sizeof(gb), "---");

        drawRow(lapNum, tb, gb,
                isBest ? LL_BEST_TIME : LL_TIME,
                isBest ? LL_FASTEST   : LL_GAP,
                false);
    }

    // ── Live current lap ──────────────────────────────────────────────────────
    if (curTime > 0.f) {
        fmtTime(curTime, tb, sizeof(tb));
        drawRow(curLap, tb, "---", LL_TIME, LL_DIM, true);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
