#include "ProLapList.h"
#include "../../core/WorldSnapshot.h"
#include "../../racing/RaceManager.h"
#include "../../network/ReplayPlayer.h"
#include "../../vehicle/Vehicle.h"
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
    char title[64] = "LAP LIST";
    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        if (const world::VehicleView* v = world::find(*snapshot, vehicleId))
            if (!v->name.empty())
                snprintf(title, sizeof(title), "LAP LIST   %s", v->name.c_str());
    }
    DrawPanelHeader(ctx, title, false, "LapList");

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
    // NoNav и здесь: строки списка кликабельны, а сфокусированное окно с
    // элементами забирает клавиатуру у транспорта повтора (см. PanelFlags).
    ImGui::BeginChild("##lapScroll", {w, scrollH}, false, ImGuiWindowFlags_NoNav);

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

    // Подсвечена РОВНО ОДНА строка — тот круг, который сейчас разбирают панели
    // (см. AnalysisLap). Раньше подсветок было две — «выбранная» и «где стоит
    // запись», — и по списку было не понять, чьи данные на экране.
    const int shownLap = AnalysisLap(vehicleId);

    // На повторе список берётся из журнала записи: там ВСЕ круги заезда, а не
    // только те, до которых доиграла запись. Оператор мотает её взад-вперёд
    // именно затем, чтобы разобрать заезд целиком, — список, меняющийся от
    // того, каким путём он пришёл в эту точку, для разбора бесполезен.
    // Строки живого круга при этом нет: круг уже лежит в списке со своим
    // итоговым временем, а где мы сейчас — показывает подсветка.
    const telemetry::VehicleJournal* journal = telemetry::replay_journal(vehicleId);
    if (journal != nullptr) {
        laps     = journal->lap_times;
        bestTime = journal->best_lap_time;
    }

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
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
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
        bool active = isCurrent;
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

        ImGui::PushID(lapNum);
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##r", {w, ROW_H});
        if (ImGui::IsItemClicked() && journal != nullptr) {
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
                journal != nullptr && shownLap == RaceConstants::OUT_LAP_NUMBER);
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
                journal != nullptr && lapNum == shownLap);
    }

    // ── Live current lap ──────────────────────────────────────────────────────
    if (journal == nullptr && curTime > 0.f) {
        fmtTime(curTime, tb, sizeof(tb));
        drawRow(curLap, tb, liveOutLap ? "OUT LAP" : "---", LL_TIME, LL_DIM, true);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
