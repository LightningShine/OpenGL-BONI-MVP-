#pragma once
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include "../ui_scale.hpp"

struct ProContext {
    ImFont* regular;   // Ubuntu Regular ~12px (menu size)
    ImFont* bold;      // Ubuntu Bold ~16px
    ImFont* title;     // Russo One ~32px (large lap time)
    ImFont* mono;      // Roboto Mono
    ImFont* russo;     // Russo One small ~13px (panel labels/numbers)
    ImFont* jb;        // JetBrains Mono Bold ~32px (sector labels/times)
    void*   logoTex;
};

namespace Pro {

// ── Time formatters ─────────────────────────────────────────────────────────
inline void fmtTime(float s, char* buf, size_t n) {
    if (s <= 0.f) { snprintf(buf, n, "--:--.---"); return; }
    int m = (int)(s / 60.f);
    float r = s - m * 60.f;
    snprintf(buf, n, "%d:%06.3f", m, r);
}
inline void fmtDelta(float d, char* buf, size_t n) {
    if (d == 0.f) { snprintf(buf, n, "---"); return; }
    snprintf(buf, n, "%+.3f", d);
}

// ── Color palette ────────────────────────────────────────────────────────────
static constexpr ImU32 COL_GOLD       = IM_COL32(218, 165,  64, 255);
static constexpr ImU32 COL_GOLD_DIM   = IM_COL32(218, 165,  64,  60);
static constexpr ImU32 COL_GREEN      = IM_COL32(  0, 210, 110, 255);
static constexpr ImU32 COL_CYAN       = IM_COL32(  0, 188, 255, 255);
static constexpr ImU32 COL_RED        = IM_COL32(255,  75,  75, 255);
static constexpr ImU32 COL_WHITE      = IM_COL32(255, 255, 255, 255);
static constexpr ImU32 COL_TEXT       = IM_COL32(220, 220, 220, 255);
static constexpr ImU32 COL_DIM        = IM_COL32(110, 110, 110, 255);
static constexpr ImU32 COL_LABEL      = IM_COL32(0x51, 0x51, 0x51, 255); // #515151
static constexpr ImU32 COL_HDR_BG     = IM_COL32( 28,  28,  28, 255);
static constexpr ImU32 COL_SEP        = IM_COL32( 48,  48,  48, 255);
static constexpr ImU32 COL_BG         = IM_COL32( 13,  13,  13, 255);
static constexpr ImU32 COL_BG_PANEL   = IM_COL32( 20,  20,  20, 255);
static constexpr ImU32 COL_BG_WIDGET  = IM_COL32( 16,  16,  16, 255);
// Sector accent colors
static constexpr ImU32 COL_S1         = IM_COL32( 85, 130, 225, 255);
static constexpr ImU32 COL_S2         = IM_COL32(165,  85, 210, 255);
static constexpr ImU32 COL_S3         = IM_COL32(220,  70,  70, 255);

// ── Layout constants ─────────────────────────────────────────────────────────
// Пункты × DPI (ui_scale): панели держат физический размер на любом мониторе
inline float pad_px()    { return ui_scale::points(10.f); } // horizontal content padding
inline float header_h()  { return ui_scale::points(26.f); } // panel header height

// Layout lock — toggled from View menu; persists per session
extern bool g_pro_layout_locked;

// Заморозка раскладки на переходные кадры смены DPI-масштаба: пока окно ОС
// догоняет новый размер, клэмп панелей и перенос позиций НЕ работают —
// иначе они «чинят» раскладку по рассинхронённому состоянию и панели
// навсегда уезжают (порча за 2 переезда между мониторами).
// Выставляет UI::apply_ui_scale_change, декрементирует UI::BeginFrame.
extern int g_layout_freeze_frames;

// Per-panel text zoom. Call once just after a panel's Begin() — handles
// Ctrl+wheel / Ctrl +/- on the focused/hovered window, persists the level to
// disk, and returns the scale factor the panel should multiply its fonts and
// paddings by. `key` must be a stable per-panel id (e.g. "LapList").
float PanelZoom(const char* key);

// Flags for all floating panels — NoMove/NoResize added when layout is locked
inline ImGuiWindowFlags PanelFlags() {
    return ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
           (g_pro_layout_locked ? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) : 0);
}

// ── Panel header ─────────────────────────────────────────────────────────────
// Draws a dark header bar at the current cursor position using DrawList
// (does not affect ImGui cursor). Then advances cursor via Dummy.
// labelFont overrides the default russo font for the header label (e.g. pass ctx.bold)
inline void DrawPanelHeader(const ProContext& ctx, const char* label,
                             bool showGear = false, ImFont* labelFont = nullptr,
                             float scale = 1.f) {
    // Keep every PRO panel on-screen: saved positions from another monitor or a
    // resolution change must not leave windows (half) outside the viewport.
    // Во время DPI-перехода клэмп выключен (см. g_layout_freeze_frames).
    if (g_layout_freeze_frames <= 0)
    {
        const ImGuiViewport* v = ImGui::GetMainViewport();
        ImVec2 ws = ImGui::GetWindowSize(), wp = ImGui::GetWindowPos();
        ImVec2 ns = {fminf(ws.x, v->WorkSize.x), fminf(ws.y, v->WorkSize.y)};
        ImVec2 np = {fminf(fmaxf(wp.x, v->WorkPos.x), v->WorkPos.x + v->WorkSize.x - ns.x),
                     fminf(fmaxf(wp.y, v->WorkPos.y), v->WorkPos.y + v->WorkSize.y - ns.y)};
        if (ns.x != ws.x || ns.y != ws.y) ImGui::SetWindowSize(ns);
        if (np.x != wp.x || np.y != wp.y) ImGui::SetWindowPos(np);
    }
    float       w  = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2      p  = ImGui::GetCursorScreenPos();

    const float hdrH = header_h();
    dl->AddRectFilled(p, {p.x + w, p.y + hdrH}, COL_HDR_BG);
    dl->AddLine({p.x, p.y + hdrH}, {p.x + w, p.y + hdrH}, COL_GOLD_DIM, 1.f);

    // Header bar height is fixed per DPI (panel layout depends on it); only the
    // label font scales with the panel zoom, clamped so it still fits the bar.
    ImFont* lf  = labelFont ? labelFont : ctx.russo;
    float   fSz = (lf ? lf->FontSize : ImGui::GetFontSize()) * scale;
    if (fSz > hdrH - 6.f) fSz = hdrH - 6.f;
    float   ty  = p.y + (hdrH - fSz) * 0.5f;
    dl->AddText(lf, fSz, {p.x + 8.f, ty}, IM_COL32(210, 210, 210, 255), label);

    if (showGear) {
        ImVec2 gc = {p.x + w - 14.f, p.y + hdrH * 0.5f};
        dl->AddCircle(gc, 6.f, COL_DIM, 8, 1.5f);
        dl->AddCircleFilled(gc, 2.2f, COL_DIM);
    }

    // Advance cursor past header (always a Dummy — avoids InvisibleButton
    // item-state side-effects that can break subsequent content rendering)
    ImGui::Dummy(ImVec2(w, hdrH + 2.f));

    // Drag: raw rect check so we never touch the ImGui item stack
    if (!g_pro_layout_locked) {
        ImVec2 wp    = ImGui::GetWindowPos();
        ImVec2 hMax  = {wp.x + w, wp.y + hdrH};
        ImVec2 click = ImGui::GetIO().MouseClickedPos[0]; // where LMB was pressed
        bool   startedInHdr = click.x >= wp.x && click.x <= wp.x + w &&
                              click.y >= wp.y && click.y <= wp.y + hdrH;
        if (ImGui::IsMouseHoveringRect(wp, hMax, false))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (startedInHdr && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos({wp.x + d.x, wp.y + d.y});
        }
    }
}

// ── Separator line ───────────────────────────────────────────────────────────
inline void DrawSep(float alpha = 1.f) {
    float w = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(p, {p.x + w, p.y}, IM_COL32(48, 48, 48, (int)(alpha * 255)), 1.f);
    ImGui::Dummy(ImVec2(w, 1.f));
}

// ── Label (Russo #515151) + right-aligned value (Ubuntu white) ───────────────
// Call from inside a Begin/End window.
inline void LabelValue(const ProContext& ctx, const char* lbl, const char* val,
                        ImU32 valCol = COL_WHITE) {
    float rightX = ImGui::GetContentRegionMax().x - pad_px();

    ImGui::SetCursorPosX(pad_px());
    if (ctx.russo) ImGui::PushFont(ctx.russo);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
    ImGui::TextUnformatted(lbl);
    ImGui::PopStyleColor();
    if (ctx.russo) ImGui::PopFont();

    // Right-align value: SameLine with absolute X so value never overflows
    float valW = ImGui::CalcTextSize(val).x;
    ImGui::SameLine(rightX - valW);
    if (ctx.regular) ImGui::PushFont(ctx.regular);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(valCol));
    ImGui::TextUnformatted(val);
    ImGui::PopStyleColor();
    if (ctx.regular) ImGui::PopFont();
}

// Main entry — called from UI::RenderProView()
void Render(const ProContext& ctx, float swipeAnim);

} // namespace Pro
