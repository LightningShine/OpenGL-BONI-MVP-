#include "ProLaptime.h"
#include "ProTrackMap.h"   // GetSectorSnapshot — секторы того же момента, что и карта
#include "../../core/WorldSnapshot.h"
#include "../../racing/RaceManager.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>

extern RaceManager* g_race_manager;

namespace Pro {

// ── Palette (from the Time.svg mockup) ──────────────────────────────────────
static constexpr ImU32 LT_LABEL = IM_COL32(0xA4, 0xA3, 0xA3, 255); // #A4A3A3
static constexpr ImU32 LT_VALUE = IM_COL32(0xF0, 0xF0, 0xF0, 255); // #F0F0F0
static constexpr ImU32 LT_GOLD  = IM_COL32(0xDA, 0xA9, 0x40, 255); // #DAA940 solid

void RenderLaptimeWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({1380.f * ui, topH + 600.f * ui}, ImGuiCond_FirstUseEver);
    // Панель вобрала в себя бывшую LAP INFO (позиция, секторы, прошлый круг),
    // поэтому выше прежних 243 пунктов.
    ImGui::SetNextWindowSize({240.f * ui,  400.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({170.f * ui, 120.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Laptime", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    // Высоту подгоняем под содержимое — см. конец функции. Пересравнение
    // добавляет и убирает строку REF, поэтому подгоняем и на его переключении:
    // иначе включённое сравнение выталкивало бы нижние строки за край окна, а
    // прокрутки в этой панели нет.
    static bool s_comparing_seen = false;
    const bool  comparing = ComparisonActive();
    const bool  fit_height = ImGui::IsWindowAppearing() || comparing != s_comparing_seen;
    s_comparing_seen = comparing;

    float       w  = ImGui::GetWindowWidth();
    float       z  = PanelZoom("Laptime");
    DrawPanelHeader(ctx, "LAPTIME", true, "Laptime"); // gear icon
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

    // Строка с двумя значениями: время у колонки значений, отклонение — по
    // правому краю. Нужна секторам и прошлому кругу: без дельты сектор сам по
    // себе не говорит ничего, сравнивать его не с чем.
    auto rowDelta = [&](const char* lbl, const char* val, ImU32 vcol,
                        const char* delta, ImU32 dcol) {
        float  rowH = valSz + 4.f * z;
        ImVec2 p    = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.regular, lblSz, {p.x + pad, p.y + (rowH - lblSz) * 0.5f}, LT_LABEL, lbl);

        const float dw = ctx.regular ? ctx.regular->CalcTextSizeA(lblSz, FLT_MAX, 0.f, delta).x
                                     : ImGui::CalcTextSize(delta).x;
        dl->AddText(ctx.regular, lblSz, {p.x + w - pad - dw, p.y + (rowH - lblSz) * 0.5f},
                    dcol, delta);

        float vw = ctx.bold ? ctx.bold->CalcTextSizeA(valSz, FLT_MAX, 0.f, val).x
                            : ImGui::CalcTextSize(val).x;
        float vx = p.x + valX;
        // Значение не наезжает на отклонение: правый предел — начало дельты.
        float maxX = p.x + w - pad - dw - 6.f * z - vw;
        if (vx > maxX) vx = maxX;
        dl->AddText(ctx.bold, valSz, {vx, p.y + (rowH - valSz) * 0.5f}, vcol, val);

        ImGui::Dummy(ImVec2(w, rowH + 3.f * z));
    };

    // ── Big LAPTIME ──────────────────────────────────────────────────────────
    float curTime = g_race_manager ? g_race_manager->GetVehicleCurrentLapTime(vehicleId) : 0.f;

    // При сравнении сверху стоит время РАЗБИРАЕМОГО КРУГА, а не бегущий таймер.
    // Иначе рядом оказывались две величины разной природы одного кегля —
    // «0:37.413» недоеденного круга против «1:33.221» законченного, — и разрыв
    // между ними читался как чудовищный. Круг ещё едет и своего времени не
    // имеет — тогда таймер и остаётся: другого числа просто нет.
    const float ownTime = (comparing && Analysed().lap_time > 0.f)
                            ? Analysed().lap_time : curTime;

    char  tb[32];
    fmtTime(ownTime, tb, sizeof(tb));

    ImGui::Dummy(ImVec2(0, 10.f * z));
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.title, ttlSz, {p.x + pad, p.y}, LT_VALUE, tb);

        // Чьё это время — подписью справа. Пока сравнения нет, вопрос не
        // возникает; как только на экране два времени, без имени они
        // неразличимы.
        if (comparing) {
            const std::shared_ptr<const world::Snapshot> snap = world::current();
            const world::VehicleView* self = world::find(*snap, vehicleId);
            char name[32];
            if (self != nullptr && !self->name.empty() && self->name != "Unknown")
                snprintf(name, sizeof(name), "%s", self->name.c_str());
            else
                snprintf(name, sizeof(name), "CAR %d", vehicleId);

            // Номер круга — как и у образца: две подписи одного вида читаются
            // как пара, разного — как две разные величины.
            char who[48];
            snprintf(who, sizeof(who), "%s L%d", name, AnalysisLap(vehicleId));

            const float ww = ctx.regular
                ? ctx.regular->CalcTextSizeA(lblSz, FLT_MAX, 0.f, who).x : 0.f;
            dl->AddText(ctx.regular, lblSz, {p.x + w - pad - ww, p.y + ttlSz * 0.55f},
                        LT_GOLD, who);
        }

        // Продвигаемся НЕ на всю высоту кегля: у цифр под базовой линией пусто
        // (выносные элементы есть у букв, а их тут нет), и полный кегль оставлял
        // под временем заметно больше воздуха, чем над ним.
        ImGui::Dummy(ImVec2(w, ttlSz * 0.80f + 4.f * z));
    }

    // ── Второй блок: круг гонщика, с которым сравнивают ──────────────────────
    //
    // ТЕМ ЖЕ КЕГЛЕМ, что и своё время. Два времени одного размера сравнимы
    // взглядом; разного — уже нет, меньшее читается как пояснение к большему.
    // Разводит их цвет, тот же, что и на графиках: золото — своё, белое —
    // образец.
    if (comparing) {
        const RefTrace& reference = Reference();

        char refBig[32];
        fmtTime(reference.lap_time, refBig, sizeof(refBig));

        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.title, ttlSz, {p.x + pad, p.y}, COL_REF, refBig);

        char who[48];
        snprintf(who, sizeof(who), "%s L%d", reference.name.c_str(), reference.ref.lap);
        const float ww = ctx.regular
            ? ctx.regular->CalcTextSizeA(lblSz, FLT_MAX, 0.f, who).x : 0.f;
        dl->AddText(ctx.regular, lblSz, {p.x + w - pad - ww, p.y + ttlSz * 0.55f},
                    COL_REF, who);

        ImGui::Dummy(ImVec2(w, ttlSz * 0.80f + 4.f * z));
    }

    goldLine();

    // ── Два разных отставания, и оба подписаны ───────────────────────────────
    //
    // Они РАЗНЫЕ по построению, и раньше это выглядело как ошибка счёта:
    //
    //   VS BEST — от собственного лучшего круга, по ЗАКРЫТЫМ СЕКТОРАМ (см.
    //             CalculateLapTimeDiffInternal). Обновляется на точках замера
    //             и между ними стоит — как секторное отставание в трансляции.
    //   VS REF  — от выбранного круга-образца, в ТОЧКЕ ТРАССЫ, где машина
    //             сейчас. Меняется непрерывно и совпадает с дорожкой DELTA на
    //             графиках: это одна и та же величина.
    //
    // Совпадать они не обязаны даже когда образец и есть лучший круг: одно
    // считается по секторам, другое — по месту на трассе.
    float delta = g_race_manager ? g_race_manager->GetVehicleLapDelta(vehicleId) : 0.f;
    char  db[32];
    fmtDelta(delta, db, sizeof(db));
    ImU32 dCol = delta < 0.f ? COL_GREEN : (delta > 0.f ? COL_RED : LT_LABEL);
    row("VS BEST", db, dCol);

    if (comparing) {
        // ИЗ ОБЩЕЙ ТОЧКИ (Pro::ComparisonDelta), а не своим счётом: раньше эта
        // строка считала отставание от ЖИВОГО таймера круга, а дорожка DELTA на
        // графиках — от ближайшего сохранённого замера. Замеры идут 10 раз в
        // секунду, поэтому числа расходились на шаг замера и выглядели как
        // ошибка счёта, хотя оба были верны.
        char  rdb[32];
        ImU32 rdCol = LT_LABEL;
        float behind = 0.f;
        if (ComparisonDelta(vehicleId, behind)) {
            fmtDelta(behind, rdb, sizeof(rdb));
            rdCol = behind < 0.f ? COL_GREEN : (behind > 0.f ? COL_RED : LT_LABEL);
        } else {
            snprintf(rdb, sizeof(rdb), "---");
        }
        row("VS REF", rdb, rdCol);
    }

    goldLine();

    // ── SPEED / ACCELE. ──────────────────────────────────────────────────────
    double speed = 0, accel = 0;
    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        if (const world::VehicleView* v = world::find(*snapshot, vehicleId)) {
            speed = v->speed_kph;
            accel = v->acceleration;
        }
    }

    // Сторона сравнения: те же величины круга-образца в той же точке трассы.
    LapInfo    refAt;
    const bool hasRef = ReferenceAtVehicle(vehicleId, refAt);

    // Справа — ЗНАЧЕНИЕ ОБРАЗЦА с подписью REF, а не разность. Голое «+0.02» не
    // отвечало на вопрос, чьё оно и от чего отсчитано; «REF 33.7» отвечает
    // сразу, и колонки читаются так же, как в панели CHANELS: слева своё,
    // справа белым — образец.
    char vb[32], rb[32];
    snprintf(vb, sizeof(vb), "%.1f Km/h", speed);
    if (hasRef) {
        snprintf(rb, sizeof(rb), "REF %.1f", refAt.speed);
        rowDelta("SPEED", vb, LT_VALUE, rb, COL_REF);
    } else {
        row("SPEED", vb, LT_VALUE);
    }
    snprintf(vb, sizeof(vb), "%.2f m/s\xc2\xb2", accel);
    if (hasRef) {
        snprintf(rb, sizeof(rb), "REF %.2f", refAt.aceleration);
        rowDelta("ACCELE.", vb, LT_VALUE, rb, COL_REF);
    } else {
        row("ACCELE.", vb, LT_VALUE);
    }

    // ── Бывшая панель LAP INFO ───────────────────────────────────────────────
    // Позиция, секторы и прошлый круг переехали сюда: всё это — про ТЕКУЩИЙ
    // круг, то есть про то же, что и большое время сверху, и держать ради них
    // отдельное окно было незачем. Номер круга не переносим: он есть в списке
    // кругов и в табло, а здесь только занимал строку.
    if (!g_race_manager) { ImGui::End(); return; }

    goldLine();

    int pos = 0;
    for (const VehicleStanding& s : g_race_manager->GetStandings())
        if (s.vehicleID == vehicleId) { pos = s.position; break; }

    char pb[16];
    if (pos > 0) snprintf(pb, sizeof(pb), "P%d", pos);
    else         snprintf(pb, sizeof(pb), "--");
    row("POSITION", pb, LT_VALUE);

    // Секторы — из того же снимка, что и виджеты на карте трассы: две панели,
    // показывающие разные секунды одного круга, читаются как ошибка счёта.
    const SectorSnapshot ss = GetSectorSnapshot(vehicleId);
    const char* sLbl[3] = { "S1", "S2", "S3" };
    for (int i = 0; i < 3; ++i) {
        char tbuf[16], dbuf[16];
        ImU32 tCol, dCol2;
        if (ss.t[i] >= 0.f) {
            int   m = (int)(ss.t[i] / 60.f);
            float r = ss.t[i] - m * 60.f;
            if (m > 0) snprintf(tbuf, sizeof(tbuf), "%d:%06.3f", m, r);
            else       snprintf(tbuf, sizeof(tbuf), "%.3f", r);
            tCol = ss.live[i] ? LT_LABEL : LT_VALUE;   // текущий сектор — приглушённый
        } else {
            snprintf(tbuf, sizeof(tbuf), "--.---");
            tCol = LT_LABEL;
        }

        if (!ss.live[i] && ss.hasDelta[i] && ss.delta[i] > 0.001f) {
            fmtDelta(ss.delta[i], dbuf, sizeof(dbuf));
            dCol2 = COL_RED;
        } else if (!ss.live[i] && ss.hasDelta[i]) {
            snprintf(dbuf, sizeof(dbuf), "BEST");
            dCol2 = COL_GREEN;
        } else {
            snprintf(dbuf, sizeof(dbuf), "---");
            dCol2 = LT_LABEL;
        }
        rowDelta(sLbl[i], tbuf, tCol, dbuf, dCol2);
    }

    char lastBuf[32];
    fmtTime(g_race_manager->GetVehiclePreviousLapTime(vehicleId), lastBuf, sizeof(lastBuf));
    rowDelta("LAST", lastBuf, LT_VALUE, db, dCol);

    // Высота ПО СОДЕРЖИМОМУ, а не заданным наперёд числом. Панель собрана из
    // строк известной высоты, поэтому «запас» снизу — это просто пустота:
    // прокрутки здесь нет, и показывать ниже LAST нечего. Считаем по курсору,
    // который строки и двигали, и подгоняем ОДИН раз при появлении окна —
    // дальше размер целиком за пользователем.
    if (fit_height) {
        const float contentH = ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y + pad;
        ImGui::SetWindowSize({ImGui::GetWindowWidth(), contentH});
    }

    ImGui::End();
}

} // namespace Pro
