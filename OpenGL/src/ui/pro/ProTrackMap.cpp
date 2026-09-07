#include "ProTrackMap.h"
#include "../ui_scale.hpp"
#include "../../core/WorldSnapshot.h"
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
#include <cfloat>
#include <algorithm>
#include <cstdio>
#include <cmath>

extern RaceManager* g_race_manager;
extern std::vector<SplinePoint> g_smooth_track_points;

namespace Pro {

// ── Sector cell palette (from Rassens.svg / Sector 1.svg) ───────────────────
struct SecStyle { ImU32 bg, text, accent; };
static constexpr SecStyle SEC_PURPLE = { IM_COL32(0x1B,0x03,0x6F,255), IM_COL32(0xBB,0x8E,0xF9,255), IM_COL32(0xBB,0x8E,0xF9,255) }; // fastest in session
static constexpr SecStyle SEC_GREEN  = { IM_COL32(0x03,0x4F,0x1B,255), IM_COL32(0x6E,0xF9,0x8E,255), IM_COL32(0x6E,0xF9,0x8E,255) }; // beats own best sector
static constexpr SecStyle SEC_YELLOW = { IM_COL32(0x6F,0x64,0x03,255), IM_COL32(0xDA,0xA5,0x40,255), IM_COL32(0xDA,0xA5,0x40,255) }; // worse than own best
static constexpr SecStyle SEC_RED    = { IM_COL32(0x6E,0x00,0x00,255), IM_COL32(0xCE,0x2B,0x2B,255), IM_COL32(0xCE,0x2B,0x2B,255) }; // slowest of all cars
static constexpr SecStyle SEC_NONE   = { IM_COL32(0x20,0x20,0x20,255), IM_COL32(0xF0,0xF0,0xF0,255), IM_COL32(0xB3,0xB3,0xB3,255) }; // no data / running

static constexpr ImU32 SEC_MARK  = IM_COL32(0xCE,0x2B,0x2B,255); // red boundary line
static constexpr float SEC_EPS   = 0.005f;
static constexpr int   HOLD_SECS = 10;        // keep finished-lap sectors after the finish

// Sector time format: "23.231" under a minute, "1:05.402" over.
static void fmtSector(float s, char* b, size_t n) {
    if (s < 0.f) { snprintf(b, n, "--.---"); return; }
    int m = (int)(s / 60.f); float r = s - m * 60.f;
    if (m > 0) snprintf(b, n, "%d:%06.3f", m, r);
    else       snprintf(b, n, "%.3f", r);
}

// All sector-time comparisons for one vehicle, so colorFor stays short.
struct SecCmp {
    float bestS[3]; bool bestV[3] = { false };   // own best PER SECTOR (all laps)
    float sessS[3]; bool sessV[3] = { false };   // fastest PER SECTOR (all cars)
    float sessW[3]; bool sessWV[3] = { false };  // slowest PER SECTOR (all cars)
};

// Purple: fastest of every run of this sector by any car. Red: the slowest.
// Green: matches/beats own best for the sector. Yellow: worse than own best.
static SecStyle colorFor(float secT, const SecCmp& c, int i) {
    if (c.sessV[i]  && secT <= c.sessS[i] + SEC_EPS)  return SEC_PURPLE;
    if (c.sessWV[i] && secT >= c.sessW[i] - SEC_EPS
                    && c.sessW[i] > c.sessS[i] + SEC_EPS) return SEC_RED;
    if (c.bestV[i]  && secT <= c.bestS[i] + SEC_EPS)  return SEC_GREEN;
    if (c.bestV[i])                                   return SEC_YELLOW;
    return SEC_NONE;
}

// Per-vehicle display state so finished-lap sectors persist for HOLD_SECS.
//
// Окно удержания живёт по часам ЗАЕЗДА (SessionTimeSeconds), а не по стенным.
// На повторе стенные часы идут, даже когда запись стоит: панель сама, без
// единого действия оператора, через десять секунд переключалась с секторов
// завершённого круга на текущий — то самое «показывает то 15 секунд, то пару».
// Границы окна храним обе: перемотка назад может унести точку записи ЛЕВЕЕ
// начала окна, и тогда удержание надо снять, а не держать вечно.
struct SecHold {
    int  lastLap = INT_MIN;
    bool has     = false;
    char tstr[3][16];
    SecStyle sty[3];
    float holdFrom  = 0.f;   // секунды заезда
    float holdUntil = 0.f;
};
static std::map<int32_t, SecHold> s_hold;

// Секторы одной машины для панелей. Общая точка для карты и LAP INFO.
//
// Читает ИЗМЕРЕННЫЕ времена — разности меток пересечения точек замера, — а не
// разбор накопленного лога телеметрии. Отсюда и главное свойство: сумма
// секторов равна времени круга, а на одной и той же точке записи числа всегда
// одни и те же. См. SECTOR_COUNT в Vehicle.h.
SectorSnapshot GetSectorSnapshot(int32_t vehicleId) {
    SectorSnapshot s;
    for (int i = 0; i < 3; ++i) { s.t[i] = -1.f; s.live[i] = false; s.delta[i] = 0.f; s.hasDelta[i] = false; }

    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    const world::VehicleView* v = world::find(*snapshot, vehicleId);
    if (v == nullptr) return s;

    // Личный лучший ПО КАЖДОМУ сектору, по завершённым кругам. Лучший сектор и
    // лучший круг — разные вещи: сектор сравнивается со своим лучшим за сессию.
    float bestS[3]; bool bestV[3] = { false, false, false };
    for (const auto& [lapNumber, lap] : v->laps_ref()) {
        for (int i = 0; i < 3; ++i) {
            const float t = lap.sectors[i];
            if (t <= 0.f) continue;
            if (!bestV[i] || t < bestS[i]) { bestS[i] = t; bestV[i] = true; }
        }
    }

    // Секторы предыдущего круга — ими закрываются клетки, до которых машина на
    // текущем круге ещё не доехала.
    const auto prevLap = v->laps_ref().find(v->current_lap_number - 1);
    const bool hasPrev = (prevLap != v->laps_ref().end());

    for (int i = 0; i < 3; ++i) {
        const float done = v->sectors[i];

        if (done > 0.f) {
            // Сектор этого круга пройден — значение окончательное.
            s.t[i] = done;
        } else if (i == v->current_sector && v->has_started_first_lap) {
            // Идущий сектор: время накапливается от входа в него.
            s.t[i] = v->current_sector_elapsed;
            s.live[i] = true;
        } else if (hasPrev && prevLap->second.sectors[i] > 0.f) {
            s.t[i] = prevLap->second.sectors[i];
        } else {
            continue;   // данных нет — панель покажет прочерк
        }

        if (!s.live[i] && bestV[i]) {
            s.delta[i] = s.t[i] - bestS[i];
            s.hasDelta[i] = true;
        }
    }
    return s;
}

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

// ── ВТОРАЯ СТРОКА БЛОКОВ: круг-образец ──────────────────────────────────────
//
// Блок сектора с одной строкой отвечает «сколько», но не отвечает «а много это
// или мало». Именно поэтому на табло большого автоспорта под временем стоит
// вторая строка соперника и разница между ними. Здесь то же: верхняя строка —
// разбираемый круг, нижняя белая — образец, под ней разница со знаком.
//
// Пустая структура (active == false) означает «сравнения нет», и блок рисуется
// ровно как раньше — ни одного лишнего пикселя.
struct CompareRow {
    bool        active = false;
    std::string name;
    char        sec[3][16] = { "", "", "" };
    char        lap[32]    = "";
    float       delta[3]   = { 0.f, 0.f, 0.f };
    bool        hasDelta[3] = { false, false, false };
    float       lapDelta    = 0.f;
    bool        hasLapDelta = false;
};

/// Разница со знаком: плюс — разбираемый круг медленнее образца. Цвет по знаку,
/// а не белый: белым написан сам образец, и белая же разница читалась бы как
/// его собственная величина.
static ImU32 deltaColor(float d) {
    return d > 0.f ? IM_COL32(0xCE,0x2B,0x2B,255)
                   : (d < 0.f ? IM_COL32(0x6E,0xF9,0x8E,255) : IM_COL32(0xB3,0xB3,0xB3,255));
}

/// Высота блока с учётом второй строки и строки разницы.
static float CardHeight(float ss, bool comparing) {
    const float base = 68.f * ss;
    return comparing ? base + (4.f + 38.f + 18.f) * ss : base;
}

// Bottom strip: Rassens.svg geometry (pos 39 | name 148 | 4 cells 147, height 68,
// header 23 + 7 gap) at a single scale `ss` — same system as the mini widgets, so
// it shrinks uniformly with the window. Name box grows to fit long names.
static void DrawStatusStrip(ImDrawList* dl, const ProContext& ctx,
                             float cx, float topY, float ss,
                             const char* driverNum, const char* driverName,
                             const char* secTime[3], const SecStyle secStyle[3],
                             const char* lapTime, const CompareRow& cmpRow) {
    ImFont* fRusso = ctx.title ? ctx.title : ctx.russo;
    ImFont* fMono  = ctx.jb ? ctx.jb : (ctx.bold ? ctx.bold : ctx.russo);
    float posW = 39.f * ss, cellW = 147.f * ss, hdrH = 23.f * ss, boxH = 38.f * ss;
    float vGap = 7.f * ss, gapS = 6.f * ss, gapC = 8.f * ss;
    float nameFsz = boxH * 0.50f;
    float nameW = fmaxf(148.f * ss,
                        fRusso->CalcTextSizeA(nameFsz, FLT_MAX, 0.f, driverName).x + 24.f * ss);
    if (cmpRow.active)
        nameW = fmaxf(nameW,
                      fRusso->CalcTextSizeA(nameFsz, FLT_MAX, 0.f, cmpRow.name.c_str()).x
                          + 24.f * ss);
    float totalW = posW + gapS + nameW + gapC + 4.f * cellW + 3.f * gapC;
    float x    = cx - totalW * 0.5f;
    float boxY = topY + hdrH + vGap;

    auto centered = [&](ImFont* f, float sz, float x0, float w0, float y0, float h0,
                        ImU32 col, const char* txt) {
        float tw = f->CalcTextSizeA(sz, FLT_MAX, 0.f, txt).x;
        dl->AddText(f, sz, {x0 + (w0 - tw) * 0.5f, y0 + (h0 - sz) * 0.5f}, col, txt);
    };

    const float rowGap = 4.f * ss;
    const float row2Y  = boxY + boxH + rowGap;
    const float dY     = row2Y + boxH;

    dl->AddRectFilled({x, boxY}, {x + posW, boxY + boxH}, IM_COL32(0x18,0x18,0x18,255));
    centered(fRusso, boxH * 0.55f, x, posW, boxY, boxH, COL_WHITE, driverNum);
    x += posW + gapS;

    dl->AddRectFilled({x, boxY}, {x + nameW, boxY + boxH}, IM_COL32(0x20,0x20,0x20,255));
    centered(fRusso, nameFsz, x, nameW, boxY, boxH, COL_GOLD, driverName);
    if (cmpRow.active) {
        dl->AddRectFilled({x, row2Y}, {x + nameW, row2Y + boxH}, IM_COL32(0x20,0x20,0x20,255));
        centered(fRusso, nameFsz, x, nameW, row2Y, boxH, COL_REF, cmpRow.name.c_str());
    }
    x += nameW + gapC;

    const char* labels[4] = { "SECTOR 1", "SECTOR 2", "SECTOR 3", "LAP TIME" };
    for (int i = 0; i < 4; ++i) {
        SecStyle st = (i < 3) ? secStyle[i] : SEC_NONE;
        const char* val = (i < 3) ? secTime[i] : lapTime;

        dl->AddRectFilled({x, topY}, {x + cellW, topY + hdrH}, IM_COL32(0x29,0x29,0x29,255));
        centered(fMono, hdrH * 0.70f, x, cellW, topY, hdrH, st.text, labels[i]);

        dl->AddRectFilled({x, boxY}, {x + cellW, boxY + boxH}, st.bg);
        dl->AddRectFilled({x, boxY}, {x + 5.f * ss, boxY + boxH}, st.accent);
        centered(fMono, boxH * 0.60f, x + 5.f * ss, cellW - 5.f * ss, boxY, boxH, st.text, val);

        if (cmpRow.active) {
            const char* rval = (i < 3) ? cmpRow.sec[i] : cmpRow.lap;
            dl->AddRectFilled({x, row2Y}, {x + cellW, row2Y + boxH}, IM_COL32(0x20,0x20,0x20,255));
            dl->AddRectFilled({x, row2Y}, {x + 5.f * ss, row2Y + boxH}, COL_REF);
            centered(fMono, boxH * 0.60f, x + 5.f * ss, cellW - 5.f * ss, row2Y, boxH,
                     COL_REF, rval);

            const bool  hasD = (i < 3) ? cmpRow.hasDelta[i] : cmpRow.hasLapDelta;
            const float d    = (i < 3) ? cmpRow.delta[i]    : cmpRow.lapDelta;
            if (hasD) {
                char db[16];
                snprintf(db, sizeof(db), "%+.3f", d);
                centered(fMono, 14.f * ss, x, cellW, dY, 18.f * ss, deltaColor(d), db);
            }
        }
        x += cellW + gapC;
    }
}

void RenderTrackMapWindow(const ProContext& ctx, int32_t vehicleId,
                           ImVec2 vpSz, float topH) {
    // Default/min sizes in points × DPI so the panel keeps its physical size
    // on any monitor (see src/ui/ui_scale.hpp).
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({210.f * ui, topH},          ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({970.f * ui, 600.f * ui},    ImGuiCond_FirstUseEver);
    // Min size keeps the map + sector widgets legible for both wide and tall tracks.
    ImGui::SetNextWindowSizeConstraints({fminf(640.f * ui, vpSz.x), fminf(480.f * ui, vpSz.y)},
                                        {vpSz.x, vpSz.y});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG));
    if (!ImGui::Begin("##TrackMap", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    float w  = ImGui::GetWindowWidth();
    float h  = ImGui::GetWindowHeight();
    float ux = w / UIConfig::BASE_WIDTH;
    float uy = h / UIConfig::BASE_HEIGHT;

    float z = PanelZoom("TrackMap");
    DrawPanelHeader(ctx, "TRACK MAP", false, "TrackMap");

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();

    // ROTATE (header, right): turns the loaded track by 90° per click.
    static int s_map_rot = 0;
    if (!g_smooth_track_points.empty()) {
        const char* rotLbl = "ROTATE";
        ImVec2 wp = ImGui::GetWindowPos();
        ImFont* rf = ctx.russo ? ctx.russo : ImGui::GetFont();
        // Кегль и отступы — в пунктах × DPI, как у подписи шапки: надпись живёт
        // в той же полосе и обязана мерить её мерой.
        float rsz = ui_scale::points(UIConfig::FONT_PT_RUSSO_SMALL);
        float tw  = rf->CalcTextSizeA(rsz, FLT_MAX, 0.f, rotLbl).x;
        // Левее крестика закрытия ровно на его шаг (см. DrawPanelHeader), плюс
        // зазор — иначе надпись упирается в крестик.
        const float gap  = ui_scale::points(28.f);
        const float textX = wp.x + w - tw - gap;
        ImVec2 bmin = {textX - ui_scale::points(4.f), wp.y};
        ImVec2 bmax = {textX + tw + ui_scale::points(4.f), wp.y + header_h()};
        bool hov = ImGui::IsMouseHoveringRect(bmin, bmax, false);
        dl->AddText(rf, rsz, {textX, wp.y + (header_h() - rsz) * 0.5f},
                    hov ? COL_WHITE : COL_LABEL, rotLbl);
        if (hov && ImGui::IsMouseClicked(0))
            s_map_rot = (s_map_rot + 1) & 3;
    }

    // Strip scale — same reference system (Rassens.svg) as the mini sector
    // widgets, so the whole bottom row shrinks uniformly with the window.
    const float ss = fminf(fmaxf(fminf(w / 1039.f, h / 550.f), 0.5f), 1.f) * 0.85f * z;

    // Полоса и блоки растут на вторую строку, только когда есть с кем
    // сравнивать: в обычном просмотре карта не должна терять высоту.
    const bool  comparing  = ComparisonActive();
    const float STRIP_H    = CardHeight(ss, comparing);
    const float TOP_GAP    = fmaxf(10.f * uy, 8.f);
    const float BOT_MARGIN = fmaxf(14.f * uy, 12.f);
    // Полоса при сравнении вдвое выше, и на низком окне она могла бы съесть всю
    // карту: отрицательная высота — это перевёрнутый прямоугольник и блоки,
    // разложенные по чужой области. Оставляем карте минимум, дальше полоса
    // просто обрежется окном.
    float mapH = fmaxf(h - header_h() - 2.f - STRIP_H - TOP_GAP - BOT_MARGIN, 80.f);
    float mapW = w;

    dl->AddRectFilled(base, {base.x + mapW, base.y + mapH}, COL_BG);

    // ── Секторы и круг: всё из снимка, всё по измеренным временам ─────────────
    // Ни одного обращения к логу телеметрии: время сектора здесь — разность
    // меток пересечения точек замера (см. SECTOR_COUNT в Vehicle.h). Поэтому
    // клетки секторов всегда складываются во время круга, а на повторе не
    // зависят от того, как оператор пришёл в эту точку записи.
    SecCmp cmp;
    float curS[3];  bool curV[3]  = { false, false, false };   // секторы этого круга
    float lastS[3]; bool lastV[3] = { false, false, false };   // предыдущего круга
    int   curLapNum = 0;
    int   curSector = 0;
    float curSectorElapsed = 0.f;
    bool  started   = false;
    std::string dname = "---";
    char  dnum[8] = "-";
    float lapPrev = g_race_manager ? g_race_manager->GetVehiclePreviousLapTime(vehicleId) : -1.f;
    if (g_race_manager)
        for (auto& st : g_race_manager->GetStandings())
            if (st.vehicleID == vehicleId && st.position > 0) {
                snprintf(dnum, sizeof(dnum), "%d", st.position);
                break;
            }
    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();

        if (const world::VehicleView* v = world::find(*snapshot, vehicleId)) {
            dname            = v->name;
            curLapNum        = v->current_lap_number;
            curSector        = v->current_sector;
            curSectorElapsed = v->current_sector_elapsed;
            started          = v->has_started_first_lap;

            for (int i = 0; i < 3; ++i)
                if (v->sectors[i] > 0.f) { curS[i] = v->sectors[i]; curV[i] = true; }

            // Личный лучший ПО КАЖДОМУ сектору за все круги: медленный сектор
            // внутри быстрого круга всё равно судится по лучшему проезду
            // именно этого сектора.
            for (const auto& [ln, lap] : v->laps_ref())
                for (int i = 0; i < 3; ++i)
                    if (lap.sectors[i] > 0.f &&
                        (!cmp.bestV[i] || lap.sectors[i] < cmp.bestS[i]))
                    { cmp.bestS[i] = lap.sectors[i]; cmp.bestV[i] = true; }

            const auto lit = v->laps_ref().find(curLapNum - 1);
            if (lit != v->laps_ref().end())
                for (int i = 0; i < 3; ++i)
                    if (lit->second.sectors[i] > 0.f) { lastS[i] = lit->second.sectors[i]; lastV[i] = true; }
        }

        // Лучший и худший проезд каждого сектора среди всех машин.
        for (const auto& [id, v] : snapshot->vehicles)
            for (const auto& [ln, lap] : v.laps_ref())
                for (int i = 0; i < 3; ++i) {
                    const float t = lap.sectors[i];
                    if (t <= 0.f) continue;
                    if (!cmp.sessV[i]  || t < cmp.sessS[i]) { cmp.sessS[i] = t; cmp.sessV[i] = true; }
                    if (!cmp.sessWV[i] || t > cmp.sessW[i]) { cmp.sessW[i] = t; cmp.sessWV[i] = true; }
                }
    }

    // ── Sector display: live current lap, finalize on cross, hold 10s ──────────
    char     secBuf[3][16];
    SecStyle secSty[3] = { SEC_NONE, SEC_NONE, SEC_NONE };
    // Те же времена ЧИСЛОМ: из них считается разница со строкой образца.
    float    secVal[3]  = { 0.f, 0.f, 0.f };
    bool     secValV[3] = { false, false, false };
    {
        const float now = SessionTimeSeconds();
        SecHold& H = s_hold[vehicleId];

        if (H.lastLap != INT_MIN && curLapNum != H.lastLap) {
            if (lastV[0] || lastV[1] || lastV[2]) {
                for (int i = 0; i < 3; ++i) {
                    if (lastV[i]) { fmtSector(lastS[i], H.tstr[i], sizeof(H.tstr[i]));
                                    H.sty[i] = colorFor(lastS[i], cmp, i); }
                    else { snprintf(H.tstr[i], sizeof(H.tstr[i]), "--.---"); H.sty[i] = SEC_NONE; }
                }
                H.holdFrom  = now;
                H.holdUntil = now + (float)HOLD_SECS;
                H.has = true;
            }
        }
        H.lastLap = curLapNum;

        if (H.has && now >= H.holdFrom && now < H.holdUntil) {
            for (int i = 0; i < 3; ++i) { snprintf(secBuf[i], sizeof(secBuf[i]), "%s", H.tstr[i]); secSty[i] = H.sty[i]; }
        } else {
            // Сектор пройден — его измеренное время; идёт сейчас — время от
            // входа в него; ещё не начинался — прочерк. Третьего не дано, и
            // «пусто между двумя заполненными» больше не появится.
            for (int i = 0; i < 3; ++i) {
                if (curV[i]) {
                    fmtSector(curS[i], secBuf[i], sizeof(secBuf[i]));
                    secSty[i] = colorFor(curS[i], cmp, i);
                    secVal[i] = curS[i]; secValV[i] = true;
                } else if (i == curSector && started) {
                    fmtSector(curSectorElapsed, secBuf[i], sizeof(secBuf[i]));
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

    // ── Mini sector widgets: live while their sector runs, then frozen until the
    //    same sector starts again on the next lap ────────────────────────────────
    char     cardBuf[3][16];
    SecStyle cardSty[3];
    float    cardVal[3]  = { 0.f, 0.f, 0.f };
    bool     cardValV[3] = { false, false, false };
    for (int i = 0; i < 3; ++i) {
        if (curV[i]) {                                         // done this lap → frozen
            const float t = curS[i];
            fmtSector(t, cardBuf[i], sizeof(cardBuf[i]));
            cardSty[i] = colorFor(t, cmp, i);
            cardVal[i] = t; cardValV[i] = true;
        } else if (i == curSector && started) {                // running now → live
            const float lt = curSectorElapsed;
            fmtSector(lt, cardBuf[i], sizeof(cardBuf[i]));
            cardSty[i] = SEC_NONE;
        } else if (lastV[i]) {                                 // holds previous lap
            fmtSector(lastS[i], cardBuf[i], sizeof(cardBuf[i]));
            cardSty[i] = colorFor(lastS[i], cmp, i);
            cardVal[i] = lastS[i]; cardValV[i] = true;
        } else {
            snprintf(cardBuf[i], sizeof(cardBuf[i]), "--.---");
            cardSty[i] = SEC_NONE;
        }
    }

    // ── Строка сравнения ──────────────────────────────────────────────────────
    CompareRow cmpRow;
    char       cardRef[3][16] = { "--.---", "--.---", "--.---" };
    float      cardDelta[3]   = { 0.f, 0.f, 0.f };
    bool       cardDeltaV[3]  = { false, false, false };

    if (comparing) {
        const RefTrace& reference = Reference();
        const RefTrace& analysed  = Analysed();

        // Разбирают ЗАКРЕПЛЁННЫЙ круг — значит и секторы показываем его, а не
        // текущие. Блок обязан говорить о том же круге, что и остальные панели,
        // иначе на одном экране два разных круга под одним именем.
        if (analysed.lap_time > 0.f) {
            for (int i = 0; i < 3; ++i) {
                if (analysed.sectors[i] <= 0.f) continue;
                fmtSector(analysed.sectors[i], secBuf[i], sizeof(secBuf[i]));
                secSty[i]  = colorFor(analysed.sectors[i], cmp, i);
                secVal[i]  = analysed.sectors[i]; secValV[i] = true;
                snprintf(cardBuf[i], sizeof(cardBuf[i]), "%s", secBuf[i]);
                cardSty[i] = secSty[i];
                cardVal[i] = secVal[i]; cardValV[i] = true;
            }
            fmtTime(analysed.lap_time, lapBuf, sizeof(lapBuf));
        }

        cmpRow.active = true;
        cmpRow.name   = reference.name;

        for (int i = 0; i < 3; ++i) {
            if (reference.sectors[i] > 0.f) {
                fmtSector(reference.sectors[i], cmpRow.sec[i], sizeof(cmpRow.sec[i]));
                snprintf(cardRef[i], sizeof(cardRef[i]), "%s", cmpRow.sec[i]);

                if (secValV[i]) {
                    cmpRow.delta[i]    = secVal[i] - reference.sectors[i];
                    cmpRow.hasDelta[i] = true;
                }
                if (cardValV[i]) {
                    cardDelta[i]  = cardVal[i] - reference.sectors[i];
                    cardDeltaV[i] = true;
                }
            } else {
                snprintf(cmpRow.sec[i], sizeof(cmpRow.sec[i]), "--.---");
            }
        }

        if (reference.lap_time > 0.f) {
            fmtTime(reference.lap_time, cmpRow.lap, sizeof(cmpRow.lap));
            if (analysed.lap_time > 0.f) {
                cmpRow.lapDelta    = analysed.lap_time - reference.lap_time;
                cmpRow.hasLapDelta = true;
            }
        } else {
            snprintf(cmpRow.lap, sizeof(cmpRow.lap), "--:--.---");
        }
    }

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
        // 90°-step rotation applied to normalized coords before fitting
        auto rotPt = [&](glm::vec2 p) -> glm::vec2 {
            switch (s_map_rot & 3) {
                case 1:  return { p.y, -p.x };
                case 2:  return { -p.x, -p.y };
                case 3:  return { -p.y, p.x };
                default: return p;
            }
        };
        glm::vec2 lo = rotPt(g_smooth_track_points[0].position), hi = lo;
        for (auto& sp : g_smooth_track_points) {
            glm::vec2 q = rotPt(sp.position);
            lo.x = lo.x < q.x ? lo.x : q.x;
            lo.y = lo.y < q.y ? lo.y : q.y;
            hi.x = hi.x > q.x ? hi.x : q.x;
            hi.y = hi.y > q.y ? hi.y : q.y;
        }
        float rX = hi.x - lo.x; if (rX < 1e-6f) rX = 1.f;
        float rY = hi.y - lo.y; if (rY < 1e-6f) rY = 1.f;
        float roadTh = fmaxf(mapH * 0.018f, 8.f);

        // Sector widget geometry — Sector 1.svg (346×68: pos 39 | name 148 | cell
        // 147 with 23px header) at the reduced in-map scale used in Track.svg.
        float cs      = fminf(fmaxf(fminf(mapW / 1039.f, mapH / 550.f), 0.55f), 1.f) * 0.72f * z;
        float cPosW   = 39.f * cs, cNameW = 148.f * cs, cCellW = 147.f * cs, cGap = 6.f * cs;
        float cCardH  = CardHeight(cs, comparing), cHdrH = 23.f * cs, cBoxH = 38.f * cs;
        float cTotalW = cPosW + cGap + cNameW + cGap + cCellW;

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
        size_t bIdx[3] = { idxAtProg(1.0 / 3.0), idxAtProg(2.0 / 3.0), 0 };

        // Each widget sits on the map side its boundary faces (0 L, 1 R, 2 T, 3 B).
        // Margins are reserved only on sides that actually hold a widget, so the
        // track itself takes all the remaining space.
        int bSide[3];
        for (int i = 0; i < 3; ++i) {
            glm::vec2 q = rotPt(g_smooth_track_points[bIdx[i]].position);
            float nx = (q.x - (lo.x + hi.x) * 0.5f) / (rX * 0.5f);
            float ny = (q.y - (lo.y + hi.y) * 0.5f) / (rY * 0.5f);
            bSide[i] = fabsf(nx) >= fabsf(ny) ? (nx >= 0.f ? 1 : 0)
                                              : (ny >= 0.f ? 2 : 3); // +y = screen top
        }
        float mL = 26.f, mR = 26.f, mT = 26.f, mB = 26.f;
        for (int i = 0; i < 3; ++i) {
            if (bSide[i] == 0) mL = cTotalW + 30.f;
            if (bSide[i] == 1) mR = cTotalW + 30.f;
            if (bSide[i] == 2) mT = cCardH  + 30.f;
            if (bSide[i] == 3) mB = cCardH  + 30.f;
        }
        float scale = fminf((mapW - mL - mR) / rX, (mapH - mT - mB) / rY);
        if (scale < 1e-3f) scale = 1e-3f;
        float offX  = base.x + mL + (mapW - mL - mR - rX*scale) * 0.5f;
        float offY  = base.y + mT + (mapH - mT - mB - rY*scale) * 0.5f;

        auto toScreen = [&](glm::vec2 p) -> ImVec2 {
            p = rotPt(p);
            return {offX + (p.x - lo.x)*scale, offY + (rY - (p.y - lo.y))*scale};
        };

        // Road: white center line + thin white edge lines (dark gaps between).
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

        // Sector widget (Sector 1.svg): position + driver name + sector cell.
        // Sits right next to the track's bounding box on its boundary's side —
        // close to the track but never on it — and is nudged along that side
        // when two widgets would overlap each other.
        ImFont* fRusso = ctx.title ? ctx.title : ctx.russo;
        ImFont* fMono  = ctx.jb ? ctx.jb : (ctx.bold ? ctx.bold : ctx.russo);
        ImVec4 placed[3]; int nPlaced = 0;
        auto drawSectorCard = [&](size_t idx, int side, const char* label,
                                  const char* timeStr, const SecStyle& st,
                                  const char* refStr, float delta, bool hasDelta) {
            ImVec2 c = toScreen(g_smooth_track_points[idx].position);
            float trackL = offX,              trackR = offX + rX * scale;
            float trackT = offY,              trackB = offY + rY * scale;
            float gapPx  = roadTh * 0.5f + 10.f;
            bool sideLR = side <= 1;
            ImVec2 p;
            switch (side) {
                case 0:  p = {trackL - gapPx - cTotalW, c.y - cCardH * 0.5f}; break;
                case 1:  p = {trackR + gapPx,           c.y - cCardH * 0.5f}; break;
                case 2:  p = {c.x - cTotalW * 0.5f, trackT - gapPx - cCardH}; break;
                default: p = {c.x - cTotalW * 0.5f, trackB + gapPx};          break;
            }
            for (int k = 0; k < nPlaced; ++k) {
                const ImVec4& r = placed[k];
                if (p.x >= r.z || p.x + cTotalW <= r.x || p.y >= r.w || p.y + cCardH <= r.y)
                    continue;
                if (sideLR) p.y = (c.y < (r.y + r.w) * 0.5f) ? r.y - cCardH - 6.f : r.w + 6.f;
                else        p.x = (c.x < (r.x + r.z) * 0.5f) ? r.x - cTotalW - 6.f : r.z + 6.f;
            }
            p.x = fminf(fmaxf(p.x, base.x + 6.f), base.x + mapW - cTotalW - 6.f);
            p.y = fminf(fmaxf(p.y, base.y + 6.f), base.y + mapH - cCardH - 6.f);
            placed[nPlaced++] = {p.x, p.y, p.x + cTotalW, p.y + cCardH};

            // Connector: boundary point → nearest point of the widget
            ImVec2 nPt = {fminf(fmaxf(c.x, p.x), p.x + cTotalW),
                          fminf(fmaxf(c.y, p.y), p.y + cCardH)};
            dl->AddLine(c, nPt, IM_COL32(0xB3,0xB3,0xB3,150), 1.f);

            // Строка разбираемого круга стоит СРАЗУ ПОД ШАПКОЙ, а не у нижнего
            // края: под ней может быть ещё строка образца и разница.
            const float boxY   = p.y + cHdrH + 7.f * cs;
            const float row2Y  = boxY + cBoxH + 4.f * cs;
            const float cDeltaY = row2Y + cBoxH;
            auto centered = [&](ImFont* f, float sz, float x0, float w0, float y0,
                                float h0, ImU32 col, const char* txt) {
                float tw = f->CalcTextSizeA(sz, FLT_MAX, 0.f, txt).x;
                dl->AddText(f, sz, {x0 + (w0 - tw)*0.5f, y0 + (h0 - sz)*0.5f}, col, txt);
            };

            float x = p.x;   // position (Russo One)
            dl->AddRectFilled({x, boxY}, {x + cPosW, boxY + cBoxH}, IM_COL32(0x18,0x18,0x18,255));
            centered(fRusso, cBoxH * 0.55f, x, cPosW, boxY, cBoxH, COL_WHITE, dnum);
            x += cPosW + cGap;
                             // driver name (Russo One), clipped to its box
            dl->AddRectFilled({x, boxY}, {x + cNameW, boxY + cBoxH}, IM_COL32(0x20,0x20,0x20,255));
            dl->PushClipRect({x, boxY}, {x + cNameW, boxY + cBoxH}, true);
            centered(fRusso, cBoxH * 0.50f, x, cNameW, boxY, cBoxH,
                     comparing ? COL_GOLD : COL_WHITE, dname.c_str());
            dl->PopClipRect();
            if (comparing) {
                dl->AddRectFilled({x, row2Y}, {x + cNameW, row2Y + cBoxH},
                                  IM_COL32(0x20,0x20,0x20,255));
                dl->PushClipRect({x, row2Y}, {x + cNameW, row2Y + cBoxH}, true);
                centered(fRusso, cBoxH * 0.50f, x, cNameW, row2Y, cBoxH,
                         COL_REF, Reference().name.c_str());
                dl->PopClipRect();
            }
            x += cNameW + cGap;
                             // sector header + time (JetBrains Mono Bold)
            dl->AddRectFilled({x, p.y}, {x + cCellW, p.y + cHdrH}, IM_COL32(0x29,0x29,0x29,255));
            centered(fMono, cHdrH * 0.70f, x, cCellW, p.y, cHdrH, st.text, label);
            dl->AddRectFilled({x, boxY}, {x + cCellW, boxY + cBoxH}, st.bg);
            dl->AddRectFilled({x, boxY}, {x + 5.f * cs, boxY + cBoxH}, st.accent);
            centered(fMono, cBoxH * 0.60f, x + 5.f * cs, cCellW - 5.f * cs, boxY, cBoxH,
                     st.text, timeStr);

            if (comparing) {
                dl->AddRectFilled({x, row2Y}, {x + cCellW, row2Y + cBoxH},
                                  IM_COL32(0x20,0x20,0x20,255));
                dl->AddRectFilled({x, row2Y}, {x + 5.f * cs, row2Y + cBoxH}, COL_REF);
                centered(fMono, cBoxH * 0.60f, x + 5.f * cs, cCellW - 5.f * cs, row2Y, cBoxH,
                         COL_REF, refStr);
                if (hasDelta) {
                    char db[16];
                    snprintf(db, sizeof(db), "%+.3f", delta);
                    centered(fMono, 14.f * cs, x, cCellW, cDeltaY, 18.f * cs,
                             deltaColor(delta), db);
                }
            }
        };

        for (int i = 0; i < 3; ++i) {
            static const char* kSectorLabels[3] = { "SECTOR 1", "SECTOR 2", "SECTOR 3" };
            drawCross(bIdx[i]);
            drawSectorCard(bIdx[i], bSide[i], kSectorLabels[i], cardBuf[i], cardSty[i],
                           cardRef[i], cardDelta[i], cardDeltaV[i]);
        }

        // Start/finish checkered flag
        ImVec2 sf = toScreen(g_smooth_track_points.front().position);
        DrawFlag(dl, {sf.x + outerTh * 0.8f, sf.y - outerTh - 14.f}, fmaxf(11.f * ux, 9.f));

        // Vehicle dot — gold circle, white outline + leader-line callout.
        // g_smooth_track_points already carry the centering offset baked in by
        // rebuildTrackCacheFromEdges, so the raw vehicle position must be
        // shifted by the same getTrackRenderOffset() to land on the track.
        // Позиция — из снимка: точка на карте обязана стоять там же, где машина
        // на главном экране, в том числе посреди перемотки повтора.
        double vx = 0, vy = 0; bool found = false;
        glm::vec2 rOff{ 0.0f, 0.0f };
        {
            const std::shared_ptr<const world::Snapshot> snapshot = world::current();
            if (const world::VehicleView* v = world::find(*snapshot, vehicleId)) {
                rOff = v->apply_track_render_offset ? getTrackRenderOffset()
                                                    : glm::vec2(0.0f, 0.0f);
                vx = v->x + rOff.x;
                vy = v->y + rOff.y;
                found = true;
            }
        }
        // Вторая машина на карте — образец в ТУ ЖЕ СЕКУНДУ круга. Расстояние
        // между точками и есть накопленный разрыв, видимый без единой цифры.
        // Сравнивать по одному и тому же МЕСТУ трассы бессмысленно: место одно,
        // и точки легли бы друг на друга.
        {
            LapInfo ghost;
            if (found && ReferenceGhost(vehicleId, ghost)) {
                // То же смещение кадра, что и у самой машины: у образца оно
                // обязано совпадать, иначе призрак поедет мимо трассы.
                ImVec2 g = toScreen({ (float)ghost.x + rOff.x, (float)ghost.y + rOff.y });
                float  gr = fmaxf(mapH * 0.013f, 6.f);
                dl->AddCircleFilled(g, gr * 0.5f, COL_REF_DIM);
                dl->AddCircle      (g, gr, COL_REF, 20, 2.f);
            }
        }

        if (found) {
            // Plain gold dot — the driver's name lives in the sector widgets.
            ImVec2 dot = toScreen({(float)vx, (float)vy});
            float  dr  = fmaxf(mapH * 0.013f, 6.f);
            dl->AddCircleFilled(dot, dr, IM_COL32(0xDA,0xA5,0x40,255));
            dl->AddCircle      (dot, dr, IM_COL32(0xDC,0xDC,0xDC,255), 20, 2.f);
        }
    }

    ImGui::SetCursorScreenPos({base.x, base.y + mapH});
    ImGui::Dummy(ImVec2(mapW, 0.f));

    // ── Bottom status strip — bare cells (no container/border), centered ───────
    const char* secT[3] = { secBuf[0], secBuf[1], secBuf[2] };
    DrawStatusStrip(dl, ctx, base.x + mapW * 0.5f, base.y + mapH + TOP_GAP, ss,
                    dnum, dname.c_str(), secT, secSty, lapBuf, cmpRow);

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg override
}

} // namespace Pro
