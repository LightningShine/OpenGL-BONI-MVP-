#include "ProGForceBars.h"
#include "../../core/WorldSnapshot.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>

namespace Pro {
namespace {

// Предел шкалы. Столбик всегда меряет одним и тем же — иначе два кадра подряд
// нельзя сравнить на глаз, а именно за этим на столбик и смотрят. Значение за
// пределом упирается в край и подсвечивается: шкала соврала, но не молча.
constexpr float RANGE_G = 2.0f;

struct Reading { float value = 0.f; bool has_vehicle = false; };

/// Перегрузка выбранной машины из опубликованного снимка — тот же момент
/// заезда, что и у карты с таблицей (и та же точка на повторе).
Reading read_g(int32_t vehicleId, bool longitudinal)
{
    Reading out;
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (const world::VehicleView* v = world::find(*snapshot, vehicleId)) {
        out.value = static_cast<float>(longitudinal ? v->g_force_y : v->g_force_x);
        out.has_vehicle = true;
    }
    return out;
}

ImU32 bar_color(float value, bool longitudinal)
{
    if (longitudinal)
        return value >= 0.f ? COL_GREEN : COL_RED;   // разгон / торможение
    return COL_RED;                                   // поворот, сторона видна по столбику
}

/// Общая часть обеих панелей: фон дорожки, ось нуля, риски шкалы.
void draw_track(ImDrawList* dl, ImVec2 min, ImVec2 max, bool vertical, float zeroPos)
{
    dl->AddRectFilled(min, max, COL_BG_WIDGET);
    dl->AddRect(min, max, IM_COL32(55, 55, 55, 255));

    // Четверти шкалы — чтобы величину можно было прикинуть, не читая цифру.
    const ImU32 tick = IM_COL32(45, 45, 45, 255);
    for (int i = 1; i < 8; ++i) {
        const float t = i / 8.0f;
        if (vertical) {
            const float y = min.y + (max.y - min.y) * t;
            dl->AddLine({min.x, y}, {max.x, y}, tick, 1.f);
        } else {
            const float x = min.x + (max.x - min.x) * t;
            dl->AddLine({x, min.y}, {x, max.y}, tick, 1.f);
        }
    }

    const ImU32 zero = IM_COL32(120, 120, 120, 255);
    if (vertical) dl->AddLine({min.x, zeroPos}, {max.x, zeroPos}, zero, 1.5f);
    else          dl->AddLine({zeroPos, min.y}, {zeroPos, max.y}, zero, 1.5f);
}

void render_bar_panel(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH,
                      bool longitudinal)
{
    const char* const key    = longitudinal ? "GForceLong"  : "GForceLat";
    const char* const title  = longitudinal ? "G-FORCE LONG" : "G-FORCE LAT";
    const char* const window = longitudinal ? "##GForceLong" : "##GForceLat";
    const char* const posLbl = longitudinal ? "ACCEL" : "RIGHT";
    const char* const negLbl = longitudinal ? "BRAKE" : "LEFT";

    const float ui = ui_scale::get();
    // Вертикальная панель узкая и высокая, горизонтальная — наоборот: столбик
    // должен идти вдоль длинной стороны, иначе шкала выходит короткой и грубой.
    const ImVec2 defSize = longitudinal ? ImVec2(130.f * ui, 300.f * ui)
                                        : ImVec2(300.f * ui, 130.f * ui);
    const ImVec2 defPos  = longitudinal ? ImVec2(1240.f * ui, topH + 600.f * ui)
                                        : ImVec2(940.f  * ui, topH + 900.f * ui);

    ImGui::SetNextWindowPos (defPos,  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(defSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({90.f * ui, 90.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin(window, nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    const float z = PanelZoom(key);
    DrawPanelHeader(ctx, title, false, key);

    const Reading reading = read_g(vehicleId, longitudinal);
    const float   value   = reading.value;
    const float   shown   = fmaxf(-RANGE_G, fminf(RANGE_G, value));
    const bool    clipped = fabsf(value) > RANGE_G;

    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.f || avail.y < 1.f) { ImGui::End(); return; }

    ImFont*     lf   = ctx.russo ? ctx.russo : ImGui::GetFont();
    ImFont*     vf   = ctx.bold  ? ctx.bold  : lf;
    const float lSz  = lf->FontSize * z;
    const float vSz  = vf->FontSize * z;
    const float pad  = pad_px() * 0.6f;

    char valBuf[24];
    snprintf(valBuf, sizeof(valBuf), "%+.2f g", value);
    const ImU32 valCol = clipped ? COL_GOLD : COL_TEXT;

    // Подписи концов шкалы: действие, а не только число. «BRAKE 2.0» читается
    // без раздумий, куда именно поехал столбик.
    char posBuf[24], negBuf[24];
    snprintf(posBuf, sizeof(posBuf), "%s %.1f", posLbl, RANGE_G);
    snprintf(negBuf, sizeof(negBuf), "%s %.1f", negLbl, RANGE_G);

    const float vw   = vf->CalcTextSizeA(vSz, FLT_MAX, 0.f, valBuf).x;
    const float posW = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, posBuf).x;
    const float negW = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, negBuf).x;

    if (longitudinal) {
        // ── Вертикальный столбик: разгон вверх, торможение вниз ──────────────
        //
        // Весь текст — в одной колонке СБОКУ от столбика, ни одной строки с
        // двумя надписями. Панель тогда можно сужать сколько угодно: тексты
        // упираются в край окна и обрезаются, но никогда не наезжают друг на
        // друга.
        const float barW = fminf(fmaxf(avail.x * 0.34f, ui_scale::points(14.f)),
                                 ui_scale::points(70.f));
        const ImVec2 tmin = {base.x + pad, base.y + pad * 0.4f};
        const ImVec2 tmax = {base.x + pad + barW, base.y + avail.y - pad * 0.4f};
        if (tmax.y - tmin.y < 4.f) { ImGui::End(); return; }

        const float zeroY = (tmin.y + tmax.y) * 0.5f;
        draw_track(dl, tmin, tmax, /*vertical=*/true, zeroY);

        const float half = (tmax.y - tmin.y) * 0.5f;
        const float barY = zeroY - (shown / RANGE_G) * half;
        dl->AddRectFilled({tmin.x + 1.f, fminf(zeroY, barY)},
                          {tmax.x - 1.f, fmaxf(zeroY, barY)},
                          bar_color(value, true));

        const float textX = tmax.x + pad;
        dl->AddText(lf, lSz, {textX, tmin.y},                        COL_LABEL, posBuf);
        dl->AddText(vf, vSz, {textX, zeroY - vSz * 0.5f},            valCol,    valBuf);
        dl->AddText(lf, lSz, {textX, tmax.y - lSz},                  COL_LABEL, negBuf);
    } else {
        // Подписи концов и значение — в одной строке под шкалой; если ширины на
        // всё трое не хватает, остаётся значение: оно и есть ответ «сколько».
        const float footH = fmaxf(lSz, vSz) + pad * 0.5f;
        const float footY = base.y + avail.y - footH;
        const bool  labels_fit = (posW + negW + vw + pad * 3.f) < avail.x;

        // ── Горизонтальный столбик: правый поворот вправо, левый влево ───────
        const ImVec2 tmin = {base.x + pad, base.y + pad * 0.4f};
        const ImVec2 tmax = {base.x + avail.x - pad, footY - pad * 0.3f};
        if (tmax.x - tmin.x < 4.f || tmax.y - tmin.y < 4.f) { ImGui::End(); return; }

        const float zeroX = (tmin.x + tmax.x) * 0.5f;
        draw_track(dl, tmin, tmax, /*vertical=*/false, zeroX);

        const float half = (tmax.x - tmin.x) * 0.5f;
        const float barX = zeroX + (shown / RANGE_G) * half;
        dl->AddRectFilled({fminf(zeroX, barX), tmin.y + 1.f},
                          {fmaxf(zeroX, barX), tmax.y - 1.f},
                          bar_color(value, false));

        if (labels_fit) {
            dl->AddText(lf, lSz, {tmin.x, footY + (footH - lSz) * 0.5f}, COL_LABEL, negBuf);
            dl->AddText(lf, lSz, {tmax.x - posW, footY + (footH - lSz) * 0.5f}, COL_LABEL, posBuf);
        }
        dl->AddText(vf, vSz, {base.x + (avail.x - vw) * 0.5f, footY + (footH - vSz) * 0.5f},
                    valCol, valBuf);
    }

    // Машины нет — показываем пустую шкалу, а не ноль: ноль означал бы, что
    // машина стоит, а её просто не выбрано.
    if (!reading.has_vehicle) {
        const char* none = "NO DATA";
        const float nw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, none).x;
        dl->AddText(lf, lSz, {base.x + (avail.x - nw) * 0.5f,
                              base.y + (avail.y - lSz) * 0.5f}, COL_DIM, none);
    }

    // Курсор до конца содержимого — иначе окно нельзя уменьшить мышью.
    ImGui::Dummy(avail);
    ImGui::End();
}

} // namespace

void RenderGForceLongWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH) {
    render_bar_panel(ctx, vehicleId, vpSz, topH, /*longitudinal=*/true);
}

void RenderGForceLatWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH) {
    render_bar_panel(ctx, vehicleId, vpSz, topH, /*longitudinal=*/false);
}

} // namespace Pro
