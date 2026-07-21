#include "ProRelative.h"
#include "../../racing/RaceManager.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>

extern RaceManager* g_race_manager;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

// Релативная карта в стиле F1-трансляции: все гонщики расставлены не на карте
// трассы, а по кольцу (по прогрессу круга), а справа — башня относительного
// времени до выбранной машины (+ впереди по трассе, − позади).

struct RelCar {
    int32_t     id;
    std::string name;
    double      progress;   // 0..1 вдоль круга
    int         position;   // классификация гонки (0 = нет)
    double      dProg;       // прогресс относительно опорной машины, [-0.5..0.5]
    float       gapTime;     // относительное время, сек (знак = dProg)
    bool        isRef;
};

// Палитра для точек на кольце (опорная всегда золотая, отдельно).
static const ImU32 RING_PAL[] = {
    IM_COL32(0x4F, 0x9D, 0xFF, 255), IM_COL32(0x5C, 0xD6, 0x8A, 255),
    IM_COL32(0xE0, 0x6C, 0x6C, 255), IM_COL32(0xB9, 0x8E, 0xF9, 255),
    IM_COL32(0x53, 0xC9, 0xC9, 255), IM_COL32(0xE8, 0xA8, 0x4B, 255),
    IM_COL32(0xD0, 0x6C, 0xB0, 255), IM_COL32(0x9A, 0xC7, 0x4B, 255),
};
static constexpr ImU32 REF_COL = IM_COL32(0xDA, 0xA5, 0x40, 255);

static void fmtGap(float g, char* b, size_t n) {
    if (fabsf(g) < 0.0005f) { snprintf(b, n, "0.000"); return; }
    snprintf(b, n, "%+.3f", g);
}

void RenderRelativeWindow(const ProContext& ctx, int32_t vehicleId,
                           ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({ 620.f * ui, topH + 40.f * ui }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({ 440.f * ui, 400.f * ui },       ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({ 300.f * ui, 240.f * ui }, { vpSz.x, vpSz.y });

    if (!ImGui::Begin("##Relative", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    // Не пускаем окно под верхний PRO-статус-бар: иначе шапка уезжает под него и
    // её нельзя ухватить, окно «застревает». topH = низ статус-бара (panelTopH).
    if (g_layout_freeze_frames <= 0) {
        ImVec2 wp = ImGui::GetWindowPos();
        if (wp.y < topH) ImGui::SetWindowPos({ wp.x, topH });
    }

    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    float z = PanelZoom("Relative");
    DrawPanelHeader(ctx, "RELATIVE MAP", false, nullptr, z, "Relative");

    // Опорное время круга: собственный лучший или предыдущий; фолбэк 30 с.
    float refLap = 30.f;
    if (g_race_manager) {
        float b = g_race_manager->GetVehicleBestLapTime(vehicleId);
        float p = g_race_manager->GetVehiclePreviousLapTime(vehicleId);
        if      (b > 1.f) refLap = b;
        else if (p > 1.f) refLap = p;
    }

    // ── Снимок всех машин ─────────────────────────────────────────────────────
    std::vector<RelCar> cars;
    double refProg = 0.0;
    bool   haveRef = false;
    {
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        auto rit = g_vehicles.find(vehicleId);
        if (rit != g_vehicles.end()) { refProg = rit->second.m_track_progress; haveRef = true; }
        for (auto& [id, v] : g_vehicles) {
            RelCar c;
            c.id = id; c.name = v.name; c.progress = v.m_track_progress;
            c.position = 0; c.isRef = (id == vehicleId);
            cars.push_back(std::move(c));
        }
    }
    if (g_race_manager)
        for (auto& st : g_race_manager->GetStandings())
            for (auto& c : cars)
                if (c.id == st.vehicleID) { c.position = st.position; break; }

    // Относительный прогресс/время к опорной машине.
    for (auto& c : cars) {
        double d = c.progress - refProg;
        if (d >  0.5) d -= 1.0;
        if (d < -0.5) d += 1.0;
        c.dProg   = d;
        c.gapTime = (float)d * refLap;
    }

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();
    float       availH = h - (base.y - ImGui::GetWindowPos().y);

    // Разметка: слева кольцо (≈58%), справа башня времени.
    float ringW  = w * 0.56f;
    float towerX = base.x + ringW;
    float towerW = w - ringW;

    // ── Кольцо (Strategy Display / относительный круг) ────────────────────────
    float cx = base.x + ringW * 0.5f;
    float cy = base.y + availH * 0.5f;
    float R  = fminf(ringW, availH) * 0.5f - 30.f * z;
    if (R < 28.f) R = 28.f;
    const double TAU = 6.28318530718;
    const double A0  = -3.14159265359 * 0.5; // прогресс 0 → верх кольца

    auto ringPos = [&](double prog, float rad) -> ImVec2 {
        double a = A0 + prog * TAU;
        return { cx + (float)cos(a) * rad, cy + (float)sin(a) * rad };
    };

    ImFont* nf  = ctx.russo   ? ctx.russo   : ImGui::GetFont();
    ImFont* lf  = ctx.regular ? ctx.regular : ImGui::GetFont();
    float   nSz = nf->FontSize * z;
    float   lSz = lf->FontSize * z;

    // Дорожка кольца тремя цветами секторов (как секторные виджеты: S1 синий,
    // S2 фиолетовый, S3 красный) — сразу видно, где какой сектор.
    const ImU32 secCol[3] = { COL_S1, COL_S2, COL_S3 };
    const float ringTh = fmaxf(10.f * z, 7.f);
    const int   SEG = 120;
    for (int i = 0; i < SEG; ++i) {
        double p0 = (double)i / SEG, p1 = (double)(i + 1) / SEG;
        int sec = (int)(p0 * 3.0); if (sec > 2) sec = 2;
        dl->AddLine(ringPos(p0, R), ringPos(p1, R), secCol[sec], ringTh);
    }
    dl->AddCircle({ cx, cy }, R, IM_COL32(0x10, 0x10, 0x10, 220), 120, 1.6f); // тёмная осевая

    // Границы секторов: риски поперёк дорожки + подписи S1/S2/S3 у середины дуг.
    for (int b = 0; b < 3; ++b) {
        double bp = (double)b / 3.0;
        dl->AddLine(ringPos(bp, R - ringTh - 1.f), ringPos(bp, R + ringTh + 1.f),
                    IM_COL32(0x18, 0x18, 0x18, 255), 2.f);
        double mp = ((double)b + 0.5) / 3.0;
        ImVec2 lp = ringPos(mp, R + 17.f * z);
        char sl[4]; snprintf(sl, sizeof(sl), "S%d", b + 1);
        float tw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, sl).x;
        dl->AddText(lf, lSz, { lp.x - tw * 0.5f, lp.y - lSz * 0.5f }, secCol[b], sl);
    }

    // Старт/финиш (прогресс 0, верх кольца): белая риска + шахматный флажок + "S/F".
    {
        dl->AddLine(ringPos(0.0, R - ringTh - 3.f), ringPos(0.0, R + ringTh + 3.f),
                    IM_COL32(0xFF, 0xFF, 0xFF, 255), 3.f);
        float cw = fmaxf(4.f * z, 3.f);
        ImVec2 fp = { cx - cw * 1.5f, cy - R - ringTh - 20.f * z };
        for (int r = 0; r < 2; ++r)
            for (int c2 = 0; c2 < 3; ++c2) {
                ImU32 cc = ((r + c2) & 1) ? IM_COL32(30, 30, 30, 255) : IM_COL32(235, 235, 235, 255);
                dl->AddRectFilled({ fp.x + c2 * cw, fp.y + r * cw },
                                  { fp.x + (c2 + 1) * cw, fp.y + (r + 1) * cw }, cc);
            }
        const char* sf = "S/F";
        float tw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, sf).x;
        dl->AddText(lf, lSz, { cx - tw * 0.5f, fp.y - lSz - 1.f }, IM_COL32(0xDC, 0xDC, 0xDC, 255), sf);
    }

    if (!haveRef || cars.empty()) {
        const char* msg = "No cars";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText({ cx - ts.x * 0.5f, cy - ts.y * 0.5f }, IM_COL32(0x64, 0x64, 0x64, 255), msg);
    }

    // ── Машины на кольце: крупные, с тёмной подложкой и номером ────────────────
    const float dotR    = fmaxf(11.f * z, 9.f);
    const float refDotR = fmaxf(14.f * z, 11.f);
    int palIdx = 0;
    for (auto& c : cars) {                          // прочие сначала
        if (c.isRef) continue;
        ImVec2 p  = ringPos(c.progress, R);
        ImU32  col = RING_PAL[palIdx++ % (int)(sizeof(RING_PAL) / sizeof(RING_PAL[0]))];
        dl->AddCircleFilled(p, dotR + 2.5f, IM_COL32(0x08, 0x08, 0x08, 255));
        dl->AddCircleFilled(p, dotR, col);
        char nb[8]; snprintf(nb, sizeof(nb), "%d", c.position > 0 ? c.position : c.id);
        float tw = nf->CalcTextSizeA(nSz, FLT_MAX, 0.f, nb).x;
        dl->AddText(nf, nSz, { p.x - tw * 0.5f, p.y - nSz * 0.5f }, IM_COL32(0x08, 0x08, 0x08, 255), nb);
    }
    for (auto& c : cars) {                          // опорная поверх, с именем в центре
        if (!c.isRef) continue;
        ImVec2 p = ringPos(c.progress, R);
        dl->AddCircleFilled(p, refDotR + 3.5f, IM_COL32(0x08, 0x08, 0x08, 255));
        dl->AddCircleFilled(p, refDotR, REF_COL);
        dl->AddCircle(p, refDotR, IM_COL32(0xFF, 0xFF, 0xFF, 255), 24, 2.f);
        char nb[8]; snprintf(nb, sizeof(nb), "%d", c.position > 0 ? c.position : c.id);
        float tw = nf->CalcTextSizeA(nSz, FLT_MAX, 0.f, nb).x;
        dl->AddText(nf, nSz, { p.x - tw * 0.5f, p.y - nSz * 0.5f }, IM_COL32(0x08, 0x08, 0x08, 255), nb);
        float tnw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, c.name.c_str()).x;
        dl->AddText(lf, lSz, { cx - tnw * 0.5f, cy - lSz * 0.5f }, REF_COL, c.name.c_str());
    }

    // ── Башня относительного времени ──────────────────────────────────────────
    // Сортировка по dProg убыв.: кто впереди по трассе — выше, опорная в центре.
    std::sort(cars.begin(), cars.end(),
              [](const RelCar& a, const RelCar& b) { return a.dProg > b.dProg; });

    dl->AddLine({ towerX, base.y }, { towerX, base.y + availH }, IM_COL32(0x30, 0x30, 0x30, 255), 1.f);

    ImFont* rf  = ctx.regular ? ctx.regular : ImGui::GetFont();
    float   rSz = rf->FontSize * z;
    float   rowH = rSz + 9.f * z;
    float   padX = 8.f * z;
    float   ty   = base.y + 4.f;
    int     palRow = 0;

    for (auto& c : cars) {
        if (ty + rowH > base.y + availH) break;
        ImVec2 rmin = { towerX + 1.f, ty }, rmax = { towerX + towerW, ty + rowH };
        if (c.isRef)
            dl->AddRectFilled(rmin, rmax, IM_COL32(0xDA, 0xA5, 0x40, 40));

        // Цветной маркер позиции слева (совпадает с кольцом).
        ImU32 mcol = c.isRef ? REF_COL
                             : RING_PAL[palRow % (int)(sizeof(RING_PAL) / sizeof(RING_PAL[0]))];
        if (!c.isRef) palRow++;
        dl->AddRectFilled({ towerX + 2.f, ty + 2.f }, { towerX + 5.f, ty + rowH - 2.f }, mcol);

        char pb[8]; snprintf(pb, sizeof(pb), "%d", c.position > 0 ? c.position : c.id);
        dl->AddText(nf, rSz, { towerX + padX, ty + (rowH - rSz) * 0.5f },
                    c.isRef ? REF_COL : IM_COL32(0x8A, 0x8A, 0x8A, 255), pb);

        // Имя (обрезается по колонке gap).
        float gapColX = towerX + towerW - 62.f * z;
        dl->PushClipRect({ towerX + padX + 20.f * z, ty }, { gapColX - 4.f, ty + rowH }, true);
        dl->AddText(rf, rSz, { towerX + padX + 20.f * z, ty + (rowH - rSz) * 0.5f },
                    c.isRef ? IM_COL32(0xFF, 0xFF, 0xFF, 255) : IM_COL32(0xC8, 0xC8, 0xC8, 255),
                    c.name.c_str());
        dl->PopClipRect();

        // Относительное время, правый край. Впереди (+) зелёный, позади (−) красный.
        char gb[16]; fmtGap(c.gapTime, gb, sizeof(gb));
        ImU32 gcol = c.isRef ? REF_COL
                   : (c.gapTime >= 0.f ? IM_COL32(0x5C, 0xD6, 0x8A, 255)
                                       : IM_COL32(0xE0, 0x6C, 0x6C, 255));
        float gw = rf->CalcTextSizeA(rSz, FLT_MAX, 0.f, gb).x;
        dl->AddText(rf, rSz, { towerX + towerW - gw - padX, ty + (rowH - rSz) * 0.5f }, gcol, gb);

        ty += rowH;
    }

    ImGui::SetCursorScreenPos({ base.x, base.y + availH });
    ImGui::Dummy({ w, 0.f });
    ImGui::End();
}

} // namespace Pro
