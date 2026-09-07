#include "ui/pro/ProLapList.h"
#include "core/WorldSnapshot.h"
#include "racing/RaceManager.h"
#include "network/ReplayPlayer.h"
#include "vehicle/Vehicle.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <mutex>

extern RaceManager* g_race_manager;

namespace Pro {

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
// Колонка двух квадратиков выбора у правого края: REF и COMPARE (см. ProView.h).
// GAP отступает влево ровно на неё, чтобы цифры и переключатели не наезжали.
// Размер выбран под попадание мышью, а не под экономию места: девять пунктов
// читались как пара точек у края списка, и промахнуться по ним было проще, чем
// попасть. Строка списка всё равно выше квадратика, так что она не растёт.
static constexpr float MARK_BOX  = 14.f;   // сторона квадратика
static constexpr float MARK_GAP  = 6.f;    // между квадратиками
static constexpr float MARK_COL  = MARK_BOX * 2.f + MARK_GAP + 8.f;
// Design width: columns are pinned to this so shrinking the window past it clips
// content on the right instead of reflowing/squeezing the columns inward.
static constexpr float REF_W    = 200.f;

void RenderLapListWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({0.f,   topH},                   ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210.f * ui, 380.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({150.f * ui, 100.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##LapList", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float z = PanelZoom("LapList");

    // ЧЬИ ЭТО КРУГИ — В ШАПКЕ.
    //
    // Список без имени машины читается как «круги заезда», и пока панель молча
    // переезжала с машины на машину, подмену было нечем заметить: цифры просто
    // становились другими. Имя стоит рядом с временами и отвечает на вопрос
    // сразу, а не после разбирательства.
    char title[96] = "LAP LIST";
    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        if (const world::VehicleView* v = world::find(*snapshot, vehicleId))
            if (!v->name.empty())
                snprintf(title, sizeof(title), "LAP LIST   %s", v->name.c_str());

        // Список можно открыть у любого гонщика, в том числе не участвующего в
        // сравнении. Пометка в шапке отвечает на вопрос «а этот тут при чём»
        // сразу, не заставляя искать закрашенный квадратик по строкам.
        const bool isRefCar = ReferenceLap().valid() && ReferenceLap().vehicle == vehicleId;
        const bool isCmpCar = ComparePin().valid()   && ComparePin().vehicle   == vehicleId;
        if (isRefCar || isCmpCar) {
            char tagged[96];
            snprintf(tagged, sizeof(tagged), "%s  - %s", title,
                     (isRefCar && isCmpCar) ? "REF + COMPARE" : (isRefCar ? "REF" : "COMPARE"));
            snprintf(title, sizeof(title), "%s", tagged);
        }
    }
    DrawPanelHeader(ctx, title, false, "LapList");

    float regSz   = (ctx.regular ? ctx.regular->FontSize : ImGui::GetFontSize()) * z;
    float russoSz = (ctx.russo   ? ctx.russo->FontSize   : ImGui::GetFontSize()) * z;
    float ROW_H   = regSz + 7.f * z;

    // Zoom-scaled layout metrics.
    float padL = PAD_L * z, lapW = LAP_W * z, colGap = COL_GAP * z;
    float padR = PAD_R * z, refW = REF_W * z, accentW = ACCENT_W * z;
    float markBox = MARK_BOX * z, markGap = MARK_GAP * z, markCol = MARK_COL * z;

    // Pin columns to the design width: when the window is wider, GAP follows the
    // right edge; when narrower, columns hold their position and clip on the right.
    float layoutW = (w > refW) ? w : refW;
    float TIME_X  = padL + lapW + colGap;
    // Место под полосу прокрутки списка держим ВСЕГДА, а не когда она появилась:
    // появляется она от длины заезда, а колонки считаются до BeginChild, когда
    // это ещё не известно. Без запаса последний квадратик на длинном списке
    // оказывался под полосой и переставал нажиматься.
    const float scrollPad = ImGui::GetStyle().ScrollbarSize;

    // Квадратики — по ФАКТИЧЕСКОМУ правому краю окна: колонки тут намеренно
    // клипятся на узком окне, но клипить переключатель значит сделать его
    // недоступным. GAP остаётся привязан к расчётной ширине и клипится как
    // прежде — он число, а не орган управления.
    // Порядок колонок как у чисел на всех панелях: сначала СВОЁ (COMPARE),
    // потом ОБРАЗЕЦ (REF). Обратный порядок заставлял помнить, где что.
    float REF_X   = fmaxf(w - padR - scrollPad - markBox, padL + lapW);  // правый — REF
    float CMP_X   = fmaxf(REF_X - markGap - markBox, padL);              // левый — COMPARE
    float GAP_R   = layoutW - padR - scrollPad - markCol;

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

        // Односимвольные шапки: колонка шириной в квадратик, слова туда не
        // влезают, а «R» и «C» над самими квадратиками читаются однозначно.
        const float rw = ctx.russo ? ctx.russo->CalcTextSizeA(russoSz, FLT_MAX, 0.f, "R").x : 6.f;
        const float cw = ctx.russo ? ctx.russo->CalcTextSizeA(russoSz, FLT_MAX, 0.f, "C").x : 6.f;
        dl->AddText(ctx.russo, russoSz, {p.x + CMP_X + (markBox - cw) * 0.5f, ty}, LL_HDR_COL, "C");
        dl->AddText(ctx.russo, russoSz, {p.x + REF_X + (markBox - rw) * 0.5f, ty}, LL_HDR_COL, "R");

        ImGui::Dummy({w, russoSz + 5.f * z});
    }
    DrawSep();

    // ── Scrollable rows ───────────────────────────────────────────────────────
    float scrollH = ImGui::GetContentRegionAvail().y;
    // NoNav и здесь: строки списка кликабельны, а сфокусированное окно с
    // элементами забирает клавиатуру у транспорта повтора (см. PanelFlags).
    ImGui::BeginChild("##lapScroll", {w, scrollH}, false, ImGuiWindowFlags_NoNav);

    // На повторе список берётся из журнала записи: там ВСЕ круги заезда, а не
    // только те, до которых доиграла запись. Оператор мотает её взад-вперёд
    // именно затем, чтобы разобрать заезд целиком, — список, меняющийся от
    // того, каким путём он пришёл в эту точку, для разбора бесполезен.
    // Строки живого круга при этом нет: круг уже лежит в списке со своим
    // итоговым временем, а где мы сейчас — показывает подсветка.
    //
    // Спрашиваем ЖУРНАЛ ПЕРВЫМ. Повтор — основной режим работы этой панели, и
    // копия живого списка кругов в нём не нужна ни разу: её тут же
    // перезаписывали журналом. Поиск в карте журналов стоит несравнимо дешевле
    // копии std::map с вектором в каждом узле.
    const std::shared_ptr<const telemetry::VehicleJournal> journal =
        telemetry::replay_journal(vehicleId);

    std::map<int, LapData> laps;
    float bestTime = -1.f;
    int   curLap   = 1;
    float curTime  = 0.f;

    if (journal != nullptr) {
        laps     = journal->lap_times;
        bestTime = journal->best_lap_time;
    }

    // Живой круг и его таймер нужны в обоих режимах: на повторе они показывают,
    // где стоит запись. Копию списка кругов берём только там, где журнала нет.
    if (g_race_manager) {
        if (journal == nullptr) {
            // Thread-safe snapshot — the network thread mutates the live lap
            // map, so we copy under the lock rather than iterating a borrowed
            // pointer.
            laps     = g_race_manager->GetVehicleLapsCopy(vehicleId);
            bestTime = g_race_manager->GetVehicleBestLapTime(vehicleId);
        }
        curLap  = g_race_manager->GetVehicleCurrentLapNumber(vehicleId);
        curTime = g_race_manager->GetVehicleCurrentLapTime(vehicleId);
    }

    // Подсвечена РОВНО ОДНА строка — тот круг, который сейчас разбирают панели
    // (см. AnalysisLap). Раньше подсветок было две — «выбранная» и «где стоит
    // запись», — и по списку было не понять, чьи данные на экране.
    const int shownLap = AnalysisLap(vehicleId);

    // Закреплённый COMPARE подсвечивает строку и в живом заезде: там журнала
    // нет, а отметка стоит и работает.
    const bool pinnedHere = ComparePin().valid() && ComparePin().vehicle == vehicleId;

    // ЕСТЬ ЛИ ВЫЕЗДНОЙ КРУГ.
    //
    // В списке времён его нет и быть не может: круг до линии не измеряется (см.
    // RaceConstants::OUT_LAP_NUMBER). Но телеметрия у него есть, и узнать про
    // него можно только по ней — поэтому спрашиваем историю замеров напрямую.
    size_t outLapSamples = 0;
    if (journal != nullptr) {
        const auto found = journal->lap_samples.find(RaceConstants::OUT_LAP_NUMBER);
        if (found != journal->lap_samples.end()) outLapSamples = found->second.size();
    } else {
        VehiclesLock lock;
        const auto vehicle = g_vehicles.find(vehicleId);
        if (vehicle != g_vehicles.end()) {
            const auto found = vehicle->second.laps.find(RaceConstants::OUT_LAP_NUMBER);
            if (found != vehicle->second.laps.end()) outLapSamples = found->second.samples.size();
        }
    }

    // ── Row draw helper ───────────────────────────────────────────────────────
    // IMPORTANT: always call ImGui::GetWindowDrawList() INSIDE the lambda so
    // we draw to the child window's draw list, not the parent's.
    auto drawRow = [&](int lapNum, const char* timeStr, const char* gapStr,
                       ImU32 timeCol, ImU32 gapCol, bool isCurrent) {
        ImVec2     p    = ImGui::GetCursorScreenPos();
        ImDrawList* dl  = ImGui::GetWindowDrawList(); // child's draw list

        // Роль этой строки в сравнении. Нужна и полоске слева, и квадратикам,
        // поэтому считается один раз здесь.
        const LapRef thisLap{ vehicleId, lapNum };
        const bool   isRef = (ReferenceLap() == thisLap);
        const bool   isCmp = (ComparePin()   == thisLap);

        bool active = isCurrent;
        bool hov    = !active &&
                      ImGui::IsMouseHoveringRect(p, {p.x + w, p.y + ROW_H}) &&
                      !ImGui::IsAnyItemActive();

        if (active)
            dl->AddRectFilled(p, {p.x + w, p.y + ROW_H}, LL_BG_SEL);
        else if (hov)
            dl->AddRectFilled(p, {p.x + w, p.y + ROW_H}, LL_BG_HOVER);

        // Полоска роли: золотая — разбираемый круг, белая — образец. Это тот же
        // цвет, которым круг нарисован на графиках, поэтому связь «строка ↔
        // кривая» видна без чтения подписей.
        if (isRef || isCmp || active)
            dl->AddRectFilled(p, {p.x + accentW, p.y + ROW_H}, isRef ? COL_REF : LL_ACCENT);

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

        // ── Квадратики REF / COMPARE ──────────────────────────────────────────
        //
        // Строка кликабельна ДО колонки квадратиков, а квадратики — своими
        // кнопками. Перекрытия нет вовсе: иначе пришлось бы полагаться на
        // порядок разбора попаданий, и промах по квадратику молча
        // перематывал бы запись на другой круг.
        const float markY = p.y + (ROW_H - markBox) * 0.5f;
        auto drawMark = [&](float x, bool on, ImU32 fill) {
            const ImVec2 a{ p.x + x, markY };
            const ImVec2 b{ a.x + markBox, a.y + markBox };
            // Невыбранный квадратик тоже с подложкой: пустая рамка на тёмном
            // фоне почти не видна, и колонка читалась как пустая.
            dl->AddRectFilled(a, b, on ? fill : IM_COL32(0x1E, 0x1E, 0x1E, 255), 3.f);
            dl->AddRect(a, b, on ? fill : IM_COL32(0x6E, 0x6E, 0x6E, 255), 3.f, 0, 1.5f);
        };
        drawMark(CMP_X, isCmp, LL_ACCENT);
        drawMark(REF_X, isRef, COL_REF);

        ImGui::PushID(lapNum);
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##r", {fmaxf(CMP_X - 2.f, 1.f), ROW_H});
        const bool rowClicked = ImGui::IsItemClicked();
        const ImVec2 afterRow = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos({p.x + CMP_X, markY});
        if (ImGui::InvisibleButton("##cmp", {markBox, markBox}))
            PinCompareLap(thisLap);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("COMPARE: the lap being analysed");

        ImGui::SetCursorScreenPos({p.x + REF_X, markY});
        if (ImGui::InvisibleButton("##ref", {markBox, markBox}))
            PinReferenceLap(thisLap);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("REF: the lap it is compared against");

        ImGui::SetCursorScreenPos(afterRow);

        if (rowClicked && journal != nullptr) {
            // Выбор — БЕЗ переключателя: щёлкнул по кругу, панели показывают
            // его. Второй щелчок по той же строке снимал выбор, и панели молча
            // возвращались к кругу повтора — с виду то же самое нажатие давало
            // разный результат.
            SelectAnalysisLap(lapNum);

            // Заодно ставим запись на начало круга: добираться с десятого круга
            // на третий, мотая руками, — работа ни о чём, а список кругов и
            // есть оглавление заезда. Метку берём у ПЕРВОГО ПО ВРЕМЕНИ замера
            // круга: история упорядочена по прогрессу, и полагаться на порядок
            // хранения тут нельзя.
            const auto samples = journal->lap_samples.find(lapNum);
            if (samples != journal->lap_samples.end() && !samples->second.empty()) {
                uint32_t start_utc = 0;
                for (const LapInfo& sample : samples->second)
                    if (sample.utc_ms != 0 && (start_utc == 0 || sample.utc_ms < start_utc))
                        start_utc = sample.utc_ms;
                if (start_utc != 0)
                    telemetry::replay_seek_to_utc(start_utc);
            }
        }
        ImGui::PopID();
    };

    char tb[32], gb[32];
    bool hasLaps = !laps.empty();

    // ── Выездной круг ─────────────────────────────────────────────────────────
    //
    // Круг ноль — то, что машина проехала ДО первого пересечения линии:
    // прогревочный проезд, выезд из боксов, круг знакомства. Времени у него нет
    // и в зачёт он не идёт, но телеметрия настоящая, и эта строка — единственный
    // способ до неё добраться: щелчок по ней перекидывает запись на начало
    // выездного круга ровно так же, как по любому другому.
    //
    // Живьём, пока машина ЕЩЁ на выездном круге, отдельная строка не нужна:
    // его показывает строка текущего круга внизу со своим бегущим временем.
    const bool liveOutLap = (journal == nullptr && curLap == RaceConstants::OUT_LAP_NUMBER);

    if (outLapSamples > 0 && !liveOutLap) {
        drawRow(RaceConstants::OUT_LAP_NUMBER, "NO TIME", "OUT LAP", LL_DIM, LL_DIM,
                (journal != nullptr || pinnedHere) &&
                shownLap == RaceConstants::OUT_LAP_NUMBER);
    }
    else if (outLapSamples == 0 && !hasLaps && curTime <= 0.f) {
        ImVec2     p  = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList(); // child's draw list
        float ty = p.y + (ROW_H - regSz)   * 0.5f;
        float cy = p.y + (ROW_H - russoSz) * 0.5f;

        float nw = ctx.russo
            ? ctx.russo->CalcTextSizeA(russoSz, FLT_MAX, 0.f, "0").x : 10.f;
        dl->AddText(ctx.russo,   russoSz, {p.x + padL + lapW - nw, cy}, LL_LAP_NUM, "0");
        dl->AddText(ctx.regular, regSz,   {p.x + TIME_X, ty},              LL_DIM,      "NO TIME");

        float ow = ctx.regular
            ? ctx.regular->CalcTextSizeA(regSz, FLT_MAX, 0.f, "WAITING").x : 50.f;
        dl->AddText(ctx.regular, regSz, {p.x + GAP_R - ow, ty}, LL_DIM, "WAITING");
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
                (journal != nullptr || pinnedHere) && lapNum == shownLap);
    }

    // ── Live current lap ──────────────────────────────────────────────────────
    if (journal == nullptr && curTime > 0.f) {
        fmtTime(curTime, tb, sizeof(tb));
        // Подсветка одна на список: закреплённый круг забирает её себе, иначе
        // светились бы две строки и было бы не понять, чьи данные на экране.
        drawRow(curLap, tb, liveOutLap ? "OUT LAP" : "---", LL_TIME, LL_DIM,
                !pinnedHere || shownLap == curLap);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
