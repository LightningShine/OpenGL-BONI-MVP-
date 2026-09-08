#include "ui/pro/ProChannels.h"
#include "core/WorldSnapshot.h"
#include "vehicle/Vehicle.h"
#include <imgui.h>
#include <cstdio>

namespace Pro {

void RenderChannelsWindow(const ProContext& ctx, int32_t vehicleId,
                           ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({0.f,   topH + 310.f * ui},      ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210.f * ui, 240.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f * ui, 80.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Channels", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float z = PanelZoom("Channels");
    DrawPanelHeader(ctx, "CHANELS", false, "Channels");
    ImGui::SetWindowFontScale(z);

    // Данные машины — из опубликованного снимка, а не из рабочего состояния
    // пайплайна: панель обязана показывать тот же момент, что и карта с
    // таблицей, и не ждать на мьютексе, пока повтор перестраивает состояние.
    double speed = 0, gx = 0, gy = 0, accel = 0, progress = 0;
    int16_t fixType = 0;
    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        if (const world::VehicleView* v = world::find(*snapshot, vehicleId)) {
            speed = v->speed_kph; gx = v->g_force_x; gy = v->g_force_y;
            accel = v->acceleration; fixType = v->fix_type;
            progress = v->track_progress;
        }
    }
    // Шкала качества решения - NMEA GGA, ровно как в протоколе
    // (rajagp/Protocol.h: "0=none, 4=RTK_FIXED") и как её читают остальные
    // потребители (UI.cpp: "fix type 4+ means RTK-grade", LapTypes.h).
    // Панель раньше подписывала 4 как "RTK Float", а 5 как "RTK Fixed" - то
    // есть на реальной записи с сантиметровой точностью инженер видел Float и
    // не верил координатам. Это единственное число, по которому судят, можно
    // ли верить сантиметрам, и оно обязано совпадать с протоколом.
    // Сторона сравнения: те же каналы круга-образца в той же точке трассы.
    // Пока образец не выбран, колонки REF нет вовсе и панель выглядит как
    // раньше — лишний столбец прочерков ничего не сообщает.
    LapInfo     refAt;
    const bool  hasRef = ReferenceAtVehicle(vehicleId, refAt);

    const char* fixLabel = fixType == 4 ? "RTK Fixed" :
                           fixType == 5 ? "RTK Float" :
                           fixType == 2 ? "DGPS"      :
                           fixType >= 1 ? "GPS"       : "No Fix";
    ImU32 fixCol = fixType == 4 ? COL_GREEN : fixType >= 1 ? COL_GOLD : COL_RED;

    char vb[24];

    // Column header
    const float colIdR   = w * 0.12f;
    const float colNameX = w * 0.17f;
    const float colValX  = hasRef ? w * 0.44f : w * 0.56f;
    const float colRefX  = w * 0.72f;

    auto colHdr = [&](const char* s, float x, bool right) {
        if (right) {
            ImGui::SetCursorPosX(x - ImGui::CalcTextSize(s).x);
        } else {
            ImGui::SetCursorPosX(x);
        }
        if (ctx.russo) ImGui::PushFont(ctx.russo);
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
        if (ctx.russo) ImGui::PopFont();
    };
    colHdr("ID", colIdR, true);
    ImGui::SameLine(colNameX); colHdr("NAME", colNameX, false);
    ImGui::SameLine(colValX);  colHdr("VALUE", colValX, false);
    if (hasRef) { ImGui::SameLine(colRefX); colHdr("REF", colRefX, false); }
    DrawSep();

    // Каналы панели — только те, что реально приходят с трекера.
    //
    // Каналы шины CAN (обороты, передача, газ, тормоз, руль, температуры,
    // топливо) отсюда убраны: такой шины у нас нет, и панель показывала ЗАШИТЫЕ
    // В КОД числа. Постоянные «9158 rpm» и «92 C» на экране инженера неотличимы
    // от настоящих данных — а решения по ним принимают всерьёз.
    //
    // Нумерация сплошная с единицы: номер здесь — порядок строки в списке, а не
    // адрес канала в каком-либо протоколе. Дыра в начале (список открывался
    // восьмёркой) читалась как «шесть каналов из тринадцати потеряны».
    struct Ch { int id; const char* name; const char* val; ImU32 valCol; const char* ref; };

    snprintf(vb, sizeof(vb), "%.1f km/h", speed); char vSpeed[24]; snprintf(vSpeed, 24, "%s", vb);
    char vGLong[24]; snprintf(vGLong, 24, "%.2f g", gy);
    char vGLat[24];  snprintf(vGLat,  24, "%.2f g", gx);
    char vAccel[24]; snprintf(vAccel, 24, "%.2f m/s\xc2\xb2", accel);
    char vProg[24];  snprintf(vProg,  24, "%.1f %%", progress * 100.0);

    // Значения образца. Формат тот же, что и у своей колонки: два числа в
    // строке сравнивают глазом, и разный вид записи этому мешает.
    char rSpeed[24] = "", rGLong[24] = "", rGLat[24] = "", rAccel[24] = "";
    if (hasRef) {
        snprintf(rSpeed, sizeof(rSpeed), "%.1f",  refAt.speed);
        snprintf(rGLong, sizeof(rGLong), "%.2f",  refAt.gForceY);
        snprintf(rGLat,  sizeof(rGLat),  "%.2f",  refAt.gForceX);
        snprintf(rAccel, sizeof(rAccel), "%.2f",  refAt.aceleration);
    }

    const Ch channels[] = {
        { 1, "Speed",     vSpeed,   COL_WHITE, rSpeed },
        { 2, "gForce Lg", vGLong,   COL_WHITE, rGLong },
        { 3, "gForce Lt", vGLat,    COL_WHITE, rGLat  },
        { 4, "Accel",     vAccel,   COL_WHITE, rAccel },
        // Тип решения и прогресс круга у образца не спрашиваем: первое —
        // свойство приёма прямо сейчас, второе — та самая точка, по которой
        // образец и выбран. Сравнивать их не с чем.
        { 5, "GPS Fix",   fixLabel, fixCol,    ""     },
        { 6, "Lap Prog",  vProg,    COL_WHITE, ""     },
    };

    float scrollH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##chanScroll", {w, scrollH}, false, ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(z);

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2      wp  = ImGui::GetWindowPos();
    char        id[8];
    const float eyeX = w - pad_px() - 4.f;

    for (auto& ch : channels) {
        snprintf(id, sizeof(id), "%d", ch.id);

        // ID (right-aligned, Russo, label color)
        {
            float tw = ImGui::CalcTextSize(id).x;
            ImGui::SetCursorPosX(colIdR - tw);
        }
        if (ctx.russo) ImGui::PushFont(ctx.russo);
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
        ImGui::TextUnformatted(id);
        ImGui::PopStyleColor();

        // Name — clipped so it doesn't overflow into value column
        ImGui::SameLine(colNameX);
        ImVec2 clipMin = {wp.x + colNameX - ImGui::GetScrollX(), wp.y};
        ImVec2 clipMax = {wp.x + colValX - 6.f,                  wp.y + 9999.f};
        ImGui::PushClipRect(clipMin, clipMax, true);
        ImGui::TextUnformatted(ch.name);
        ImGui::PopClipRect();
        if (ctx.russo) ImGui::PopFont();

        // Value (Ubuntu)
        ImGui::SameLine(colValX);
        if (ctx.regular) ImGui::PushFont(ctx.regular);
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(ch.valCol));
        ImGui::TextUnformatted(ch.val);
        ImGui::PopStyleColor();

        if (hasRef && ch.ref[0] != '\0') {
            ImGui::SameLine(colRefX);
            ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_REF));
            ImGui::TextUnformatted(ch.ref);
            ImGui::PopStyleColor();
        }
        if (ctx.regular) ImGui::PopFont();

        // Decorative eye icon
        {
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            float cy = (rmin.y + rmax.y) * 0.5f;
            float cx = wp.x - ImGui::GetScrollX() + eyeX;
            dl->AddCircle   ({cx, cy}, 5.f,  COL_DIM, 12, 1.f);
            dl->AddCircleFilled({cx, cy}, 2.f, COL_DIM);
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
