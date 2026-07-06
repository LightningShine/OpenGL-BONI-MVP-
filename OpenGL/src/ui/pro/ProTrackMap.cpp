#include "ProTrackMap.h"
#include "../../racing/RaceManager.h"
#include "../../rendering/Interpolation.h"
#include "../../vehicle/Vehicle.h"
#include "../UI_Config.h"
#include <imgui.h>
#include <mutex>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <climits>
#include <algorithm>
#include <cstdio>
#include <cmath>

extern RaceManager* g_race_manager;
extern std::vector<SplinePoint> g_smooth_track_points;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

// ── Sector cell palette (from Rassens.svg / Sector 1.svg) ───────────────────
struct SecStyle { ImU32 bg, text, accent; };
static constexpr SecStyle SEC_PURPLE = { IM_COL32(0x1B,0x03,0x6F,255), IM_COL32(0xBB,0x8E,0xF9,255), IM_COL32(0xBB,0x8E,0xF9,255) }; // fastest in session
static constexpr SecStyle SEC_GREEN  = { IM_COL32(0x03,0x4F,0x1B,255), IM_COL32(0x6E,0xF9,0x8E,255), IM_COL32(0x6E,0xF9,0x8E,255) }; // beats own best sector
static constexpr SecStyle SEC_YELLOW = { IM_COL32(0x6F,0x64,0x03,255), IM_COL32(0xDA,0xA5,0x40,255), IM_COL32(0xDA,0xA5,0x40,255) }; // < 1s off best sector
static constexpr SecStyle SEC_RED    = { IM_COL32(0x6E,0x00,0x00,255), IM_COL32(0xCE,0x2B,0x2B,255), IM_COL32(0xCE,0x2B,0x2B,255) }; // > 1s off best sector
static constexpr SecStyle SEC_NONE   = { IM_COL32(0x20,0x20,0x20,255), IM_COL32(0xF0,0xF0,0xF0,255), IM_COL32(0xB3,0xB3,0xB3,255) }; // no data / running

static constexpr ImU32 SEC_MARK  = IM_COL32(0xCE,0x2B,0x2B,255); // red boundary line
static constexpr float SEC_EPS   = 0.005f;
static constexpr int   HOLD_SECS = 10;        // keep finished-lap sectors after the finish
// Reference cell proportions from the SVG mockups (value 147 × total 68).
static constexpr float CELL_RATIO = 147.f / 68.f;

// Sector time format: "23.231" under a minute, "1:05.402" over.
static void fmtSector(float s, char* b, size_t n) {
    if (s < 0.f) { snprintf(b, n, "--.---"); return; }
    int m = (int)(s / 60.f); float r = s - m * 60.f;
    if (m > 0) snprintf(b, n, "%d:%06.3f", m, r);
    else       snprintf(b, n, "%.3f", r);
}

// Time at which each sector boundary (0, 1/3, 2/3, 1) was first crossed in a lap.
// bt[i] = -1 when not reached yet. Robust to noisy/non-monotonic progress.
static void crossTimes(const std::vector<LapInfo>& s, float bt[4]) {
    for (int i = 0; i < 4; ++i) bt[i] = -1.f;
    if (s.size() < 2) return;
    double prevMaxP = s.front().progress;
    float  prevT    = s.front().timefromstart;
    int    nb       = 0;
    while (nb <= 3 && (double)nb / 3.0 <= prevMaxP) bt[nb++] = prevT;
    for (size_t i = 1; i < s.size(); ++i) {
        double p = s[i].progress; if (p < prevMaxP) p = prevMaxP;
        float  t = s[i].timefromstart;
        while (nb <= 3 && (double)nb / 3.0 <= p) {
            double bp = (double)nb / 3.0, den = p - prevMaxP;
            double f = den > 1e-9 ? (bp - prevMaxP) / den : 1.0;
            bt[nb++] = prevT + (t - prevT) * (float)f;
        }
        prevMaxP = p; prevT = t;
    }
}

// Sector times for a COMPLETED lap. The final boundary (progress 1.0) is rarely
// sampled exactly — the lap ends at the finish line — so sector 3 is closed off
// with the lap's recorded total time.
static void secCompleted(const std::vector<LapInfo>& s, float lapTime,
                         float out[3], bool v[3]) {
    float bt[4]; crossTimes(s, bt);
    if (bt[3] < 0.f && lapTime > 0.f && bt[2] >= 0.f) bt[3] = lapTime;
    for (int k = 0; k < 3; ++k) {
        if (bt[k] >= 0.f && bt[k + 1] >= 0.f) { out[k] = bt[k + 1] - bt[k]; v[k] = true; }
        else { out[k] = 0.f; v[k] = false; }
    }
}

static void accMin(float dst[3], bool dv[3], const float z[3], const bool zv[3]) {
    for (int k = 0; k < 3; ++k)
        if (zv[k] && (!dv[k] || z[k] < dst[k])) { dst[k] = z[k]; dv[k] = true; }
}

// Color a sector vs the driver's best-ever time for THAT sector (bestS) and the
// overall session best for that sector (sessS).
static SecStyle colorFor(float secT, const float bestS[3], const bool bestV[3],
                         const float sessS[3], const bool sessV[3], int i) {
    if (sessV[i] && secT <= sessS[i] + SEC_EPS)    return SEC_PURPLE;
    if (bestV[i] && secT - bestS[i] <= SEC_EPS)    return SEC_GREEN;
    if (bestV[i] && secT - bestS[i] <  1.0f)       return SEC_YELLOW;
    if (bestV[i])                                  return SEC_RED;
    return SEC_NONE;
}

// Per-vehicle display state so finished-lap sectors persist for HOLD_SECS.
struct SecHold {
    int  lastLap = INT_MIN;
    bool has     = false;
    char tstr[3][16];
    SecStyle sty[3];
    std::chrono::steady_clock::time_point holdUntil;
};
static std::map<int32_t, SecHold> s_hold;

// Small checkered start/finish flag with a short pole (p = top of pole).
static void DrawFlag(ImDrawList* dl, ImVec2 p, float sz) {
    int cols = 3, rows = 4; float cw = sz / cols;
    dl->AddLine({p.x, p.y}, {p.x, p.y + sz * 1.5f}, IM_COL32(220,220,220,255), 1.5f);
    ImVec2 o = {p.x + 1.5f, p.y};
    dl->AddRectFilled(o, {o.x + cw * cols, o.y + cw * rows}, IM_COL32(25,25,25,255));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (((r + c) & 1) == 0)
                dl->AddRectFilled({o.x + c*cw, o.y + r*cw}, {o.x + (c+1)*cw, o.y + (r+1)*cw},
                                  IM_COL32(220,220,220,255));
}

// Strip geometry, sized from available width/height with a max cell size so the
// cells keep the reference aspect ratio instead of stretching.
struct StripLayout { float carW, nameW, cellW, cellGap, contentW; };
static StripLayout computeStrip(float availW, float cellH, float ux) {
    StripLayout L;
    L.carW    = fmaxf(38.f * ux, 32.f);
    L.nameW   = fmaxf(120.f * ux, 92.f);
    L.cellGap = 4.f;
    float fixed    = L.carW + 2.f + L.nameW + 8.f;
    float maxCellW = cellH * CELL_RATIO;
    float share    = (availW - fixed - L.cellGap * 3.f) / 4.f;
    L.cellW    = fminf(share, maxCellW); if (L.cellW < 40.f) L.cellW = 40.f;
    L.contentW = fixed + 4.f * L.cellW + L.cellGap * 3.f;
    return L;
}

// Bottom strip: car number + name + 3 colored sector cells + LAP TIME cell.
static void DrawStatusStrip(ImDrawList* dl, const ProContext& ctx,
                             ImVec2 base, float stripH, const StripLayout& L,
                             const char* driverNum, const char* driverName,
                             const char* secTime[3], const SecStyle secStyle[3],
                             const char* lapTime, float scale) {
    float fSz  = (ctx.russo ? ctx.russo->FontSize : 12.f) * scale;
    float hdrH = fmaxf(stripH * 0.34f, 16.f);
    float gap  = 3.f;
    float valY = base.y + hdrH + gap;
    float valH = stripH - hdrH - gap;
    float x = base.x;

    dl->AddRectFilled({x, valY}, {x + L.carW, valY + valH}, IM_COL32(0x18,0x18,0x18,255));
    float nW = ImGui::CalcTextSize(driverNum).x;
    dl->AddText(ctx.bold ? ctx.bold : ctx.russo, fSz * 1.2f,
                {x + (L.carW - nW) * 0.5f, valY + (valH - fSz * 1.2f) * 0.5f}, COL_WHITE, driverNum);
    x += L.carW + 2.f;

    dl->AddRectFilled({x, valY}, {x + L.nameW, valY + valH}, IM_COL32(0x20,0x20,0x20,255));
    dl->AddText(ctx.bold ? ctx.bold : ctx.russo, fSz,
                {x + 8.f, valY + (valH - fSz) * 0.5f}, COL_WHITE, driverName);
    x += L.nameW + 8.f;

    const char* labels[4] = { "SECTOR 1", "SECTOR 2", "SECTOR 3", "LAP TIME" };
    for (int i = 0; i < 4; ++i) {
        SecStyle st = (i < 3) ? secStyle[i] : SEC_NONE;
        const char* val = (i < 3) ? secTime[i] : lapTime;

        dl->AddRectFilled({x, base.y}, {x + L.cellW, base.y + hdrH}, IM_COL32(0x29,0x29,0x29,255));
        dl->AddText(ctx.russo, fSz * 0.8f, {x + 8.f, base.y + (hdrH - fSz * 0.8f) * 0.5f},
                    COL_WHITE, labels[i]);

        dl->AddRectFilled({x, valY}, {x + L.cellW, valY + valH}, st.bg);
        dl->AddRectFilled({x, valY}, {x + 4.f, valY + valH}, st.accent);
        dl->AddText(ctx.bold ? ctx.bold : ctx.russo, fSz * 1.15f,
                    {x + 12.f, valY + (valH - fSz * 1.15f) * 0.5f}, st.text, val);
        x += L.cellW + L.cellGap;
    }
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

    float z = PanelZoom("TrackMap");
    DrawPanelHeader(ctx, "TRACK MAP", false, nullptr, z);

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();

    // Floating strip: capped height, margins on every side, off the bottom edge.
    const float STRIP_H    = fminf(fmaxf(66.f * uy, 58.f), 80.f) * z;
    const float TOP_GAP    = fmaxf(10.f * uy, 8.f);
    const float BOT_MARGIN = fmaxf(14.f * uy, 12.f);
    const float SIDE_MARGIN= fmaxf(w * 0.035f, 16.f);
    const float STRIP_PAD  = 6.f;
    float mapH = h - HDR_H - 2.f - STRIP_H - TOP_GAP - BOT_MARGIN;
    float mapW = w;

    dl->AddRectFilled(base, {base.x + mapW, base.y + mapH}, COL_BG);

    // ── Gather lap/sector data under the vehicles lock ─────────────────────────
    float curBt[4]; for (int i = 0; i < 4; ++i) curBt[i] = -1.f;
    float bestS[3]; bool bestV[3] = { false };   // personal best PER SECTOR (all laps)
    float sessS[3]; bool sessV[3] = { false };   // overall best PER SECTOR (all cars)
    float lastS[3]; bool lastV[3] = { false };
    int   curLapNum = 0;
    double curProg  = 0.0;
    float curTimer  = 0.f;
    std::string dname = "---";
    char  dnum[8] = "?";
    float lapPrev = g_race_manager ? g_race_manager->GetVehiclePreviousLapTime(vehicleId) : -1.f;
    {
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        auto lapTimeOf = [](Vehicle& v, int ln) -> float {
            auto m = v.m_laps.find(ln);
            return (m != v.m_laps.end()) ? m->second.lapTime : -1.f;
        };
        auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            Vehicle& v = it->second;
            dname     = v.name;
            snprintf(dnum, sizeof(dnum), "%d", vehicleId);
            curLapNum = v.m_current_lap_number;
            curProg   = v.m_track_progress;
            curTimer  = v.m_current_lap_timer;

            auto cit = v.laps.find(curLapNum);
            if (cit != v.laps.end()) crossTimes(cit->second.samples, curBt);

            // Best PER SECTOR across EVERY lap (theoretical-best sectors), so a slow
            // sector inside an overall-fast lap is still judged against the fastest
            // that sector has ever been driven.
            for (auto& [ln, sess] : v.laps) {
                float z[3]; bool zv[3]; secCompleted(sess.samples, lapTimeOf(v, ln), z, zv);
                accMin(bestS, bestV, z, zv);
            }
            auto lit = v.laps.find(curLapNum - 1);
            if (lit != v.laps.end()) secCompleted(lit->second.samples, lapTimeOf(v, curLapNum - 1), lastS, lastV);
        }
        for (auto& [id, v] : g_vehicles)
            for (auto& [ln, sess] : v.laps) {
                float z[3]; bool zv[3]; secCompleted(sess.samples, lapTimeOf(v, ln), z, zv);
                accMin(sessS, sessV, z, zv);
            }
    }

    // ── Sector display: live current lap, finalize on cross, hold 10s ──────────
    char     secBuf[3][16];
    SecStyle secSty[3] = { SEC_NONE, SEC_NONE, SEC_NONE };
    {
        auto now = std::chrono::steady_clock::now();
        SecHold& H = s_hold[vehicleId];

        if (H.lastLap != INT_MIN && curLapNum != H.lastLap) {
            if (lastV[0] || lastV[1] || lastV[2]) {
                for (int i = 0; i < 3; ++i) {
                    if (lastV[i]) { fmtSector(lastS[i], H.tstr[i], sizeof(H.tstr[i]));
                                    H.sty[i] = colorFor(lastS[i], bestS, bestV, sessS, sessV, i); }
                    else { snprintf(H.tstr[i], sizeof(H.tstr[i]), "--.---"); H.sty[i] = SEC_NONE; }
                }
                H.holdUntil = now + std::chrono::seconds(HOLD_SECS);
                H.has = true;
            }
        }
        H.lastLap = curLapNum;

        if (H.has && now < H.holdUntil) {
            for (int i = 0; i < 3; ++i) { snprintf(secBuf[i], sizeof(secBuf[i]), "%s", H.tstr[i]); secSty[i] = H.sty[i]; }
        } else {
            float sc[3]; bool sv[3];
            for (int k = 0; k < 3; ++k) {
                if (curBt[k] >= 0.f && curBt[k + 1] >= 0.f) { sc[k] = curBt[k + 1] - curBt[k]; sv[k] = true; }
                else { sc[k] = 0.f; sv[k] = false; }
            }
            int active = (int)(curProg * 3.0); if (active < 0) active = 0; if (active > 2) active = 2;
            for (int i = 0; i < 3; ++i) {
                if (sv[i]) {
                    fmtSector(sc[i], secBuf[i], sizeof(secBuf[i]));
                    secSty[i] = colorFor(sc[i], bestS, bestV, sessS, sessV, i);
                } else if (i == active && curBt[i] >= 0.f) {
                    float lt = curTimer - curBt[i]; if (lt < 0.f) lt = 0.f;
                    fmtSector(lt, secBuf[i], sizeof(secBuf[i]));
                    secSty[i] = SEC_NONE;
                } else {
                    snprintf(secBuf[i], sizeof(secBuf[i]), "--.---");
                    secSty[i] = SEC_NONE;
                }
            }
        }
    }
    char lapBuf[32];
    if (lapPrev > 0.f) fmtTime(lapPrev, lapBuf, sizeof(lapBuf));
    else               snprintf(lapBuf, sizeof(lapBuf), "--:--.---");

    // ── Track drawing ──────────────────────────────────────────────────────────
    if (g_smooth_track_points.empty()) {
        const char* msg = "No track loaded — drag a .trk2 file here";
        ImVec2 tSz = ImGui::CalcTextSize(msg);
        ImGui::SetCursorScreenPos({base.x + (mapW - tSz.x) * 0.5f, base.y + (mapH - tSz.y) * 0.5f});
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
        ImGui::TextUnformatted(msg);
        ImGui::PopStyleColor();
    } else {
        const size_t n = g_smooth_track_points.size();
        glm::vec2 lo = g_smooth_track_points[0].position, hi = lo;
        for (auto& sp : g_smooth_track_points) {
            lo.x = lo.x < sp.position.x ? lo.x : sp.position.x;
            lo.y = lo.y < sp.position.y ? lo.y : sp.position.y;
            hi.x = hi.x > sp.position.x ? hi.x : sp.position.x;
            hi.y = hi.y > sp.position.y ? hi.y : sp.position.y;
        }
        float rX = hi.x - lo.x; if (rX < 1e-6f) rX = 1.f;
        float rY = hi.y - lo.y; if (rY < 1e-6f) rY = 1.f;
        float pad = fmaxf(80.f * ux, 64.f);   // room for off-track sector cards
        float scale = fminf((mapW - pad*2) / rX, (mapH - pad*2) / rY);
        float offX  = base.x + (mapW - rX*scale) * 0.5f;
        float offY  = base.y + (mapH - rY*scale) * 0.5f;
        ImVec2 mapCenter = {base.x + mapW * 0.5f, base.y + mapH * 0.5f};

        auto toScreen = [&](glm::vec2 p) -> ImVec2 {
            return {offX + (p.x - lo.x)*scale, offY + (rY - (p.y - lo.y))*scale};
        };

        // Cumulative arc length → so visual boundaries match the arc-length-based
        // lap progress used for timing (point indices are NOT evenly spaced).
        std::vector<float> cum(n, 0.f);
        for (size_t i = 1; i < n; ++i) {
            glm::vec2 d = g_smooth_track_points[i].position - g_smooth_track_points[i-1].position;
            cum[i] = cum[i-1] + sqrtf(d.x*d.x + d.y*d.y);
        }
        glm::vec2 dc = g_smooth_track_points[0].position - g_smooth_track_points[n-1].position;
        float total = cum[n-1] + sqrtf(dc.x*dc.x + dc.y*dc.y); if (total < 1e-6f) total = 1.f;
        auto idxAtProg = [&](double prog) -> size_t {
            float target = (float)prog * total;
            for (size_t i = 0; i < n; ++i) if (cum[i] >= target) return i;
            return n - 1;
        };

        // Road: white center line + thin white edge lines (dark gaps between).
        float roadTh  = fmaxf(mapH * 0.018f, 8.f);
        float outerTh = roadTh;            // outer white (forms the two edge lines)
        float midTh   = roadTh * 0.66f;    // dark gap
        float innerTh = roadTh * 0.30f;    // white center line
        const ImU32 white = IM_COL32(0xDC,0xDC,0xDC,255);
        const ImU32 dark  = IM_COL32(0x1A,0x1A,0x1A,255);
        auto drawRing = [&](float th, ImU32 col) {
            for (size_t i = 1; i < n; ++i)
                dl->AddLine(toScreen(g_smooth_track_points[i-1].position),
                            toScreen(g_smooth_track_points[i].position), col, th);
            if (n > 2)
                dl->AddLine(toScreen(g_smooth_track_points.back().position),
                            toScreen(g_smooth_track_points.front().position), col, th);
        };
        drawRing(outerTh, white);
        drawRing(midTh,   dark);
        drawRing(innerTh, white);

        auto drawCross = [&](size_t idx) {
            size_t a = (idx + n - 1) % n, b = (idx + 1) % n;
            ImVec2 pa = toScreen(g_smooth_track_points[a].position);
            ImVec2 pb = toScreen(g_smooth_track_points[b].position);
            ImVec2 d = {pb.x - pa.x, pb.y - pa.y};
            float L = sqrtf(d.x*d.x + d.y*d.y); if (L < 1e-3f) return;
            ImVec2 perp = {-d.y / L, d.x / L};
            ImVec2 c = toScreen(g_smooth_track_points[idx].position);
            float hl = outerTh * 0.6f + 3.f;
            dl->AddLine({c.x - perp.x*hl, c.y - perp.y*hl},
                        {c.x + perp.x*hl, c.y + perp.y*hl}, SEC_MARK, 2.f);
        };

        // Neutral, upright sector indicator card placed fully off the track, sized
        // to the reference aspect ratio with a max size so it does not stretch.
        auto drawSectorCard = [&](size_t idx, const char* label, const char* timeStr) {
            ImVec2 c = toScreen(g_smooth_track_points[idx].position);
            ImVec2 dir = {c.x - mapCenter.x, c.y - mapCenter.y};
            float L = sqrtf(dir.x*dir.x + dir.y*dir.y); if (L < 1e-3f) { dir = {0,-1}; L = 1; }
            dir = {dir.x / L, dir.y / L};

            float cardW = fminf(fmaxf(175.f * ux, 120.f), 156.f) * z;
            float cardH = cardW / CELL_RATIO;
            float hdrH  = cardH * (23.f / 68.f);
            float valH  = cardH - hdrH;
            float hdrFsz = fminf(fmaxf(hdrH * 0.78f, 11.f), 17.f);
            float valFsz = fminf(fmaxf(valH * 0.70f, 16.f), 30.f);

            float off = outerTh + 8.f + 0.5f * sqrtf(cardW*cardW + cardH*cardH);
            ImVec2 ctr = {c.x + dir.x*off, c.y + dir.y*off};
            ImVec2 p   = {ctr.x - cardW*0.5f, ctr.y - cardH*0.5f};

            dl->AddLine(c, {c.x + dir.x*(outerTh + 4.f), c.y + dir.y*(outerTh + 4.f)},
                        IM_COL32(0xB3,0xB3,0xB3,150), 1.f);
            dl->AddRectFilled(p, {p.x + cardW, p.y + hdrH}, IM_COL32(0x29,0x29,0x29,255));
            dl->AddText(ctx.russo, hdrFsz, {p.x + 7.f, p.y + (hdrH - hdrFsz)*0.5f}, COL_WHITE, label);
            ImVec2 v0 = {p.x, p.y + hdrH};
            dl->AddRectFilled(v0, {p.x + cardW, p.y + cardH}, IM_COL32(0x20,0x20,0x20,255));
            dl->AddRectFilled(v0, {v0.x + 4.f, p.y + cardH}, IM_COL32(0xB3,0xB3,0xB3,255));
            dl->AddText(ctx.bold ? ctx.bold : ctx.russo, valFsz,
                        {p.x + 11.f, v0.y + (valH - valFsz)*0.5f}, COL_WHITE, timeStr);
        };

        size_t i1 = idxAtProg(1.0 / 3.0);
        size_t i2 = idxAtProg(2.0 / 3.0);
        drawCross(i1); drawSectorCard(i1, "SECTOR 1", secBuf[0]);
        drawCross(i2); drawSectorCard(i2, "SECTOR 2", secBuf[1]);
        drawCross(0);  drawSectorCard(0,  "SECTOR 3", secBuf[2]);

        // Start/finish checkered flag
        ImVec2 sf = toScreen(g_smooth_track_points.front().position);
        DrawFlag(dl, {sf.x + outerTh * 0.8f, sf.y - outerTh - 14.f}, fmaxf(11.f * ux, 9.f));

        // Vehicle dot — gold circle, white outline + leader-line callout.
        double vx = 0, vy = 0; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_vehicles_mutex);
            auto vit = g_vehicles.find(vehicleId);
            if (vit != g_vehicles.end()) { vx = vit->second.m_normalized_x; vy = vit->second.m_normalized_y; found = true; }
        }
        if (found) {
            ImVec2 dot = toScreen({(float)vx, (float)vy});
            float  dr  = fmaxf(mapH * 0.013f, 6.f);
            ImVec2 k1 = {dot.x + 18.f * ux, dot.y - 14.f * ux};
            ImVec2 k2 = {k1.x + 60.f * ux,  k1.y};
            dl->AddLine(dot, k1, IM_COL32(235,235,235,200), 1.f);
            dl->AddLine(k1,  k2, IM_COL32(235,235,235,200), 1.f);
            float fSz = (ctx.russo ? ctx.russo->FontSize : 12.f) * z;
            dl->AddText(ctx.bold ? ctx.bold : ctx.russo, fSz, {k1.x + 2.f, k1.y - fSz - 2.f}, COL_WHITE, dname.c_str());
            dl->AddCircleFilled(dot, dr, IM_COL32(0xDA,0xA5,0x40,255));
            dl->AddCircle      (dot, dr, IM_COL32(0xDC,0xDC,0xDC,255), 20, 2.f);
        }
    }

    ImGui::SetCursorScreenPos({base.x, base.y + mapH});
    ImGui::Dummy(ImVec2(mapW, 0.f));

    // ── Bottom status strip — floating element, sized to content + centered ────
    float cellH = STRIP_H - 2.f * STRIP_PAD;
    StripLayout L = computeStrip(mapW - 2.f * SIDE_MARGIN - 2.f * STRIP_PAD, cellH, ux);
    float containerW = L.contentW + 2.f * STRIP_PAD;
    ImVec2 outP = {base.x + (mapW - containerW) * 0.5f, base.y + mapH + TOP_GAP};
    dl->AddRectFilled(outP, {outP.x + containerW, outP.y + STRIP_H}, IM_COL32(0x12,0x12,0x12,255), 5.f);
    dl->AddRect      (outP, {outP.x + containerW, outP.y + STRIP_H}, COL_GOLD_DIM, 5.f, 0, 1.f);

    const char* secT[3] = { secBuf[0], secBuf[1], secBuf[2] };
    DrawStatusStrip(dl, ctx, {outP.x + STRIP_PAD, outP.y + STRIP_PAD}, cellH, L,
                    dnum, dname.c_str(), secT, secSty, lapBuf, z);

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg override
}

} // namespace Pro
