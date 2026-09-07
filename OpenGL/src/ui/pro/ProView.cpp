#include "ProView.h"
#include "ProLapList.h"
#include "ProChannels.h"
#include "ProSessionInfo.h"
#include "ProGForce.h"
#include "ProGForceBars.h"
#include "ProTrackMap.h"
#include "ProTrackReport.h"
#include "ProGraphs.h"
#include "ProLaptime.h"
#include "ProEvents.h"
#include "ProSectors.h"
#include "ProRelative.h"
#include "../../core/WorldSnapshot.h"
#include "../../network/ReplayPlayer.h"
#include "../../vehicle/Vehicle.h"
#include "../../racing/RaceManager.h"
#include "../../racing/StopReset/StartStop.h"
#include "../UI_Config.h"
#include <imgui.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <fstream>

extern RaceManager* g_race_manager;
extern int g_focused_vehicle_id;

namespace Pro {

bool g_pro_layout_locked = false;
int  g_layout_freeze_frames = 0;

// ── Per-panel text zoom (persisted to pro_scales.ini) ───────────────────────
static std::unordered_map<std::string, float> g_panelScale;
static bool g_scalesLoaded = false;
static const char* kScaleFile = "pro_scales.ini";

static void loadScales() {
    if (g_scalesLoaded) return;
    g_scalesLoaded = true;
    std::ifstream f(kScaleFile);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        float v = (float)atof(line.c_str() + eq + 1);
        if (v > 0.3f && v < 5.f) g_panelScale[line.substr(0, eq)] = v;
    }
}

// Изменения копятся в памяти, на диск уходят паузой (см. FlushPanelSettings).
static bool   g_scalesDirty   = false;
static double g_scalesTouched = 0.0;

static void writeScales() {
    std::ofstream f(kScaleFile, std::ios::trunc);
    if (!f) return;
    for (const auto& [k, v] : g_panelScale) f << k << "=" << v << "\n";
}

static void markScalesDirty() {
    g_scalesDirty   = true;
    g_scalesTouched = ImGui::GetTime();
}

/// Пишет накопленные масштабы, если пора. Возвращает false, если нечего писать.
bool FlushPanelScales(bool force) {
    if (!g_scalesDirty) return false;

    // Пауза в вводе: прокрутка колеса идёт очередью щелчков, и писать файл на
    // каждый из них — то же самое, что писать его в цикле отрисовки.
    constexpr double SETTLE_SECONDS = 0.75;
    if (!force && ImGui::GetTime() - g_scalesTouched < SETTLE_SECONDS) return false;

    writeScales();
    g_scalesDirty = false;
    return true;
}

float PanelZoom(const char* key) {
    loadScales();
    auto it = g_panelScale.find(key);
    float sc = (it != g_panelScale.end()) ? it->second : 1.f;

    ImGuiIO& io = ImGui::GetIO();
    bool changed = false;
    if (ImGui::IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.f) {
        sc += io.MouseWheel * 0.08f; changed = true;
    }
    if (ImGui::IsWindowFocused() && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))      { sc += 0.1f; changed = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) { sc -= 0.1f; changed = true; }
    }
    if (changed) {
        if (sc < 0.6f) sc = 0.6f; if (sc > 2.5f) sc = 2.5f;
        g_panelScale[key] = sc;
        markScalesDirty();
    }
    return sc;
}

float HeaderButton(const ProContext& ctx, const char* label, bool active, bool arrow,
                   float rightEdge, bool& clicked) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();

    ImFont*     f    = ctx.russo ? ctx.russo : ImGui::GetFont();
    const float fSz  = ui_scale::points(UIConfig::FONT_PT_RUSSO_SMALL);
    const float tw   = f->CalcTextSizeA(fSz, FLT_MAX, 0.f, label).x;
    const float ah   = arrow ? ui_scale::points(4.f) : 0.f;
    const float gap  = arrow ? ui_scale::points(5.f) : 0.f;
    const float full = tw + gap + (arrow ? ah * 2.f : 0.f);
    const float x    = rightEdge - full;
    const float grow = ui_scale::points(4.f);

    const bool hov = ImGui::IsMouseHoveringRect({x - grow, wp.y},
                                                {x + full + grow, wp.y + header_h()}, false);
    const ImU32 col = (active || hov) ? COL_HDR_TEXT : COL_LABEL;
    dl->AddText(f, fSz, {x, wp.y + (header_h() - fSz) * 0.5f}, col, label);

    if (arrow) {
        const float ax = x + tw + gap;
        const float ay = wp.y + header_h() * 0.5f;
        dl->AddTriangleFilled({ax, ay - ah * 0.5f}, {ax + ah * 2.f, ay - ah * 0.5f},
                              {ax + ah, ay + ah * 0.9f}, col);
    }

    clicked = hov && ImGui::IsMouseClicked(0);
    return x;
}

void PushDropdownStyle() {
    const ImVec2 dsz = ImGui::GetIO().DisplaySize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {UIConfig::DROPDOWN_PADDING_X * dsz.x,
                         UIConfig::DROPDOWN_PADDING_Y * dsz.y});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UIConfig::DROPDOWN_BORDER_SIZE);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   UIConfig::DROPDOWN_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        {UIConfig::DROPDOWN_ITEM_SPACING_X * dsz.x,
                         UIConfig::DROPDOWN_ITEM_SPACING_Y * dsz.y});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {UIConfig::DROPDOWN_ITEM_PADDING_X * dsz.x,
                         UIConfig::DROPDOWN_ITEM_PADDING_Y * dsz.y});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(UIConfig::DROPDOWN_BG_R, UIConfig::DROPDOWN_BG_G,
                                                   UIConfig::DROPDOWN_BG_B, UIConfig::DROPDOWN_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(UIConfig::DROPDOWN_BORDER_R, UIConfig::DROPDOWN_BORDER_G,
                                                   UIConfig::DROPDOWN_BORDER_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text,    ImVec4(UIConfig::DROPDOWN_TEXT_R, UIConfig::DROPDOWN_TEXT_G,
                                                   UIConfig::DROPDOWN_TEXT_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Header,  ImVec4(UIConfig::DROPDOWN_HOVER_R, UIConfig::DROPDOWN_HOVER_G,
                                                   UIConfig::DROPDOWN_HOVER_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(UIConfig::DROPDOWN_HOVER_R, UIConfig::DROPDOWN_HOVER_G,
                                                         UIConfig::DROPDOWN_HOVER_B, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(UIConfig::DROPDOWN_ACTIVE_R, UIConfig::DROPDOWN_ACTIVE_G,
                                                         UIConfig::DROPDOWN_ACTIVE_B, 1.f));
}

void PopDropdownStyle() {
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(5);
}

float SessionTimeSeconds() {
    // На повторе часы сессии показывают, сколько оператор смотрит запись, а не
    // когда событие случилось в заезде: сессия стартует в момент открытия
    // файла. Поэтому берём позицию в записи.
    if (telemetry::replay_is_active())
        return telemetry::replay_status().position_ms / 1000.0f;

    return g_race_manager ? g_race_manager->GetRaceElapsedTime() : 0.f;
}

// ── Круг для разбора (см. ProView.h) ────────────────────────────────────────
static int s_analysis_lap = -1;   // -1 — идти за повтором

// Закреплённые стороны сравнения. Живут дольше выбора строкой: щелчок по строке
// снимается, как только запись тронулась, а закреплённый круг — только тем же
// квадратиком, сменой записи или закрытием повтора. Иначе сравнение нельзя было
// бы просто ПОСМОТРЕТЬ в движении: нажал «играть» — и образец пропал.
static LapRef s_ref_pin;
static LapRef s_compare_pin;

int AnalysisLap(int32_t vehicleId) {
    // Закреплённая сторона COMPARE главнее всего: её выбрали квадратиком, а
    // квадратик — это «держи этот круг», в отличие от щелчка по строке, который
    // живёт только пока запись стоит.
    if (s_compare_pin.valid() && s_compare_pin.vehicle == vehicleId)
        return s_compare_pin.lap;

    // Ноль — ВЫЕЗДНОЙ круг, он выбирается наравне с остальными; «нет выбора»
    // это -1 (см. RaceConstants::OUT_LAP_NUMBER).
    if (s_analysis_lap >= 0) return s_analysis_lap;
    return g_race_manager ? g_race_manager->GetVehicleCurrentLapNumber(vehicleId) : 0;
}

void SelectAnalysisLap(int lapNumber) { s_analysis_lap = lapNumber; }

LapRef ReferenceLap() { return s_ref_pin; }
LapRef ComparePin()   { return s_compare_pin; }

int32_t AnalysisVehicle(int32_t displayVehicleId) {
    if (!s_compare_pin.valid()) return displayVehicleId;

    // Машины из закрепления может уже не быть на трассе (закрыли запись,
    // участник сошёл) — тогда разбираем то, что выбрано, а не пустоту.
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (world::find(*snapshot, s_compare_pin.vehicle) == nullptr)
        return displayVehicleId;

    return s_compare_pin.vehicle;
}

void PinReferenceLap(const LapRef& lap) {
    s_ref_pin = (s_ref_pin == lap) ? LapRef{} : lap;
}

void PinCompareLap(const LapRef& lap) {
    s_compare_pin = (s_compare_pin == lap) ? LapRef{} : lap;
}

void ClearComparison() {
    s_ref_pin     = LapRef{};
    s_compare_pin = LapRef{};
}

// ── Круги сравнения ─────────────────────────────────────────────────────────
// Обе стороны снимаются одинаково и живут рядом: любая разница в том, КАК они
// прочитаны, тут же вылезает разными числами на соседних панелях.
static RefTrace s_ref_trace;
static RefTrace s_cmp_trace;

bool RefTrace::at(double d, LapInfo& out) const {
    if (samples.empty()) return false;

    // За пределами покрытия образца честнее не показывать ничего. Ближайший
    // край подошёл бы по типу, но соврал бы по существу: «скорость образца на
    // этом месте» там просто не измерена.
    //
    // Допуск — примерно полшага между замерами: на 10 Гц и круге в полминуты
    // это доли процента круга. Первый замер круга не попадает ровно на 0.000, а
    // последний — на 1.000, и без допуска образец пропадал бы у самой линии,
    // ровно там, где сравнение и интересно.
    constexpr double EDGE = 0.005;
    if (d < samples.front().progress - EDGE || d > samples.back().progress + EDGE)
        return false;

    const auto hi = std::lower_bound(
        samples.begin(), samples.end(), d,
        [](const LapInfo& sample, double value) { return sample.progress < value; });

    if (hi == samples.begin()) { out = samples.front(); return true; }

    if (hi == samples.end()) {
        // Хвост круга после последнего замера. Значения каналов там взять
        // неоткуда — держим последние измеренные, — а вот ВРЕМЯ известно точно:
        // это время круга, посчитанное по меткам пересечения линии. Тянемся к
        // нему, иначе на финише образец выглядит быстрее, чем был.
        out = samples.back();
        out.progress = d;
        if (lap_time > samples.back().timefromstart) {
            // Скобки вокруг std::min здесь обязательны: windows.h тянет за
            // собой макрос min, и без них имя разбирается как вызов макроса.
            const double span = 1.0 - samples.back().progress;
            const double t = (span > 1e-9)
                ? (std::min)((d - samples.back().progress) / span, 1.0) : 1.0;
            out.timefromstart = samples.back().timefromstart +
                static_cast<float>((lap_time - samples.back().timefromstart) * t);
        }
        return true;
    }

    const LapInfo& b = *hi;
    const LapInfo& a = *std::prev(hi);
    const double   span = b.progress - a.progress;
    const float    t = (span > 1e-9) ? static_cast<float>((d - a.progress) / span) : 0.f;

    out = a;
    out.progress      = d;
    out.speed         = a.speed         + (b.speed         - a.speed)         * t;
    out.gForceX       = a.gForceX       + (b.gForceX       - a.gForceX)       * t;
    out.gForceY       = a.gForceY       + (b.gForceY       - a.gForceY)       * t;
    out.aceleration   = a.aceleration   + (b.aceleration   - a.aceleration)   * t;
    out.timefromstart = a.timefromstart + (b.timefromstart - a.timefromstart) * t;
    return true;
}

bool RefTrace::at_time(float seconds, LapInfo& out) const {
    if (samples.empty()) return false;
    if (seconds < samples.front().timefromstart || seconds > samples.back().timefromstart)
        return false;

    // ПЕРЕБОРОМ, а не двоичным поиском. История упорядочена по ПРОГРЕССУ — на
    // этом инварианте стоит весь остальной разбор, — а про время внутри круга
    // такого обещания никто не давал: достаточно один раз отъехать назад, и
    // порядок по времени сломается. std::lower_bound на неупорядоченном
    // диапазоне — неопределённое поведение, а не просто неточный ответ. Вызов
    // здесь один на кадр, поэтому перебор ничего не стоит.
    for (size_t i = 1; i < samples.size(); ++i) {
        const LapInfo& a = samples[i - 1];
        const LapInfo& b = samples[i];
        if (seconds < a.timefromstart || seconds > b.timefromstart) continue;

        const float span = b.timefromstart - a.timefromstart;
        const float t = (span > 1e-6f) ? (seconds - a.timefromstart) / span : 0.f;

        out = a;
        out.timefromstart = seconds;
        out.progress = a.progress + (b.progress - a.progress) * t;
        out.x        = a.x + (b.x - a.x) * t;
        out.y        = a.y + (b.y - a.y) * t;
        out.speed    = a.speed + (b.speed - a.speed) * t;
        return true;
    }
    return false;
}

const RefTrace& Reference() { return s_ref_trace; }
const RefTrace& Analysed()  { return s_cmp_trace; }

bool ComparisonActive() { return s_ref_trace.valid && !s_ref_trace.samples.empty(); }

bool ReferenceGhost(int32_t vehicleId, LapInfo& out) {
    if (!ComparisonActive()) return false;

    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    const world::VehicleView* view = world::find(*snapshot, vehicleId);
    if (view == nullptr || view->current_lap_timer <= 0.f) return false;

    // Призрак идёт от таймера ТЕКУЩЕГО круга, поэтому показывать его можно
    // только когда разбирают именно его. На закреплённом старом круге секунда
    // на таймере относится к другому кругу, и вторая машина оказалась бы
    // призраком неизвестно чего.
    if (AnalysisLap(vehicleId) != view->current_lap_number) return false;

    return s_ref_trace.at_time(view->current_lap_timer, out);
}

bool ComparisonDelta(int32_t vehicleId, float& out) {
    if (!ComparisonActive() || !s_cmp_trace.valid) return false;

    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    const world::VehicleView* view = world::find(*snapshot, vehicleId);
    if (view == nullptr) return false;

    LapInfo mine, other;
    if (!s_cmp_trace.at(view->track_progress, mine))  return false;
    if (!s_ref_trace.at(view->track_progress, other)) return false;

    out = mine.timefromstart - other.timefromstart;
    return true;
}

bool ReferenceAtVehicle(int32_t vehicleId, LapInfo& out) {
    if (!ComparisonActive()) return false;

    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    const world::VehicleView* view = world::find(*snapshot, vehicleId);
    if (view == nullptr) return false;

    return s_ref_trace.at(view->track_progress, out);
}

namespace {

/// Обрезает имя до `max_bytes`, НЕ РАЗРУБАЯ символ UTF-8.
///
/// Имена приходят из реестра устройств, то есть от пользователя, и длина у них
/// произвольная. Подпись «REF <имя> L7» стоит в узкой строке панели LAPTIME и с
/// длинным именем наезжала бы на значение. Резать по байтам нельзя: имя не
/// обязано быть латиницей, а половина многобайтового символа выводится мусором.
std::string short_name(std::string name, size_t max_bytes)
{
    if (name.size() <= max_bytes) return name;

    size_t cut = max_bytes;
    // Продолжение символа UTF-8 — байт вида 10xxxxxx. Отступаем назад до начала.
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
    name.resize(cut);
    return name;
}

/// Откуда берутся замеры круга-образца — оттуда же, откуда их берут панели
/// разбора: журнал записи на повторе, история машины в живом заезде. Второго
/// источника заводить нельзя, иначе образец и разбираемый круг оказались бы
/// посчитаны по-разному.
///
/// Возвращает размер источника, не копируя его: по нему видно, не подрос ли
/// круг с прошлого кадра.
size_t reference_source_size(const LapRef& lap,
                             std::shared_ptr<const telemetry::VehicleJournal>& journal) {
    journal = telemetry::replay_journal(lap.vehicle);
    if (journal != nullptr) {
        const auto found = journal->lap_samples.find(lap.lap);
        return (found == journal->lap_samples.end()) ? 0 : found->second.size();
    }

    VehiclesLock lock;
    const auto vehicle = g_vehicles.find(lap.vehicle);
    if (vehicle == g_vehicles.end()) return 0;
    const auto found = vehicle->second.laps.find(lap.lap);
    return (found == vehicle->second.laps.end()) ? 0 : found->second.samples.size();
}

}  // namespace

namespace {

/// Пересобирает `trace`, если запрошенный круг сменился или подрос.
///
/// Обе стороны сравнения проходят через одну эту функцию намеренно: стоит
/// прочитать их по-разному, и на соседних панелях появятся два числа, каждое
/// «правильное» по-своему.
void refresh_trace(RefTrace& trace, const LapRef& want)
{
    if (!want.valid()) {
        if (trace.valid || trace.ref.valid()) trace = RefTrace{};
        return;
    }

    std::shared_ptr<const telemetry::VehicleJournal> journal;
    const size_t size = reference_source_size(want, journal);

    // Пересобираем, только если круг ДРУГОЙ или подрос. Сравниваем ЗАПРОС, а не
    // результат: круг без замеров даёт valid == false, и проверка по нему
    // перечитывала бы источник каждый кадр — вместе с мьютексом машин. На
    // повторе круг не растёт вовсе, поэтому копия делается ровно один раз.
    if (trace.ref == want && trace.source_size == size)
        return;

    RefTrace built;
    built.ref         = want;
    built.source_size = size;

    if (journal != nullptr) {
        const auto found = journal->lap_samples.find(want.lap);
        if (found != journal->lap_samples.end()) built.samples = found->second;

        const auto time = journal->lap_times.find(want.lap);
        if (time != journal->lap_times.end()) {
            built.lap_time = time->second.lapTime;
            built.sectors  = time->second.sectors;
        }

        built.name = journal->name;
    } else {
        VehiclesLock lock;
        const auto vehicle = g_vehicles.find(want.vehicle);
        if (vehicle != g_vehicles.end()) {
            const auto found = vehicle->second.laps.find(want.lap);
            if (found != vehicle->second.laps.end()) built.samples = found->second.samples;

            const auto time = vehicle->second.m_laps.find(want.lap);
            if (time != vehicle->second.m_laps.end()) {
                built.lap_time = time->second.lapTime;
                built.sectors  = time->second.sectors;
            }

            built.name = vehicle->second.name;
        }
    }

    if (built.name.empty() || built.name == "Unknown")
        built.name = "CAR " + std::to_string(want.vehicle);
    built.name = short_name(std::move(built.name), 14);

    built.valid = !built.samples.empty();
    trace = std::move(built);
}

}  // namespace

void RefreshReference(int32_t displayVehicleId) {
    // Сменилась запись — образец из неё к новой отношения не имеет.
    static uint64_t s_session_seen = 0;
    if (ReplaySessionChanged(s_session_seen)) {
        s_ref_pin     = LapRef{};
        s_compare_pin = LapRef{};
        s_ref_trace   = RefTrace{};
        s_cmp_trace   = RefTrace{};
    }

    refresh_trace(s_ref_trace, s_ref_pin);

    // Разбираемая сторона нужна только когда есть с чем сравнивать: без
    // образца её никто не читает, а копировать круг каждый кадр незачем.
    LapRef analysed;
    if (s_ref_trace.valid) {
        const int32_t car = AnalysisVehicle(displayVehicleId);
        if (car >= 0) analysed = LapRef{ car, AnalysisLap(car) };
    }
    refresh_trace(s_cmp_trace, analysed);
}

bool ReplaySessionChanged(uint64_t& seen) {
    const uint64_t now = telemetry::replay_session_revision();
    if (now == seen) return false;
    seen = now;
    return true;
}

// ── Машина, которую разбирают панели ────────────────────────────────────────
// ДЕРЖИТСЯ, ПОКА ОНА НА ТРАССЕ.
//
// Раньше здесь каждый кадр брался ЛИДЕР таблицы. Лидер по ходу заезда меняется
// — а на повторе ещё и на каждой перемотке, потому что откат пересчитывает
// позиции, — и панели молча переезжали на другую машину. На экране это
// выглядело так, будто у одного и того же заезда прыгают времена кругов: щёлк
// по кругу — список показывает 1:34.130 / 1:33.880, щёлк ещё раз — 1:33.221 /
// 1:33.513. Обе пары настоящие, просто от РАЗНЫХ машин.
//
// Выбор машины — дело оператора (боковое меню, клик по машине). Пока он не
// выбрал, берём одну и не отпускаем: переезжать самостоятельно панель разбора
// не имеет права.
static int32_t s_display_vehicle = -1;

static bool vehicleIsOnTrack(int32_t vehicleId) {
    if (vehicleId == -1) return false;
    VehiclesLock lock;
    return g_vehicles.find(vehicleId) != g_vehicles.end();
}

static int32_t getDisplayVehicleId() {
    if (g_focused_vehicle_id != -1) {
        s_display_vehicle = g_focused_vehicle_id;
        return s_display_vehicle;
    }

    if (vehicleIsOnTrack(s_display_vehicle))
        return s_display_vehicle;

    // Машины не стало (другая запись, начало заезда) — выбираем заново.
    if (g_race_manager) {
        auto standings = g_race_manager->GetStandings();
        if (!standings.empty()) {
            s_display_vehicle = standings.front().vehicleID;
            return s_display_vehicle;
        }
    }
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (!snapshot->vehicles.empty()) {
        s_display_vehicle = snapshot->vehicles.begin()->first;
        return s_display_vehicle;
    }
    s_display_vehicle = -1;
    return -1;
}

void Render(const ProContext& ctx, float swipeAnim) {
    ImGuiViewport* vp   = ImGui::GetMainViewport();
    const ImVec2   sz   = vp->Size;
    const float    topH = UIConfig::top_bar_px();
    const float    botH = UIConfig::bottom_bar_px();

    // Swipe-in animation — solid dark panel slides from left, panels hidden
    if (swipeAnim < 0.99f) {
        float panelX = vp->Pos.x + sz.x * (swipeAnim - 1.f);
        ImGui::SetNextWindowPos({panelX, vp->Pos.y + topH});
        ImGui::SetNextWindowSize({sz.x, sz.y - topH - botH});
        ImGui::SetNextWindowBgAlpha(1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG));
        ImGui::Begin("##ProAnim", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize       |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar    |
            ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    // Открылась (или закрылась) другая запись — весь выбор оператора к ней не
    // относится: номера машин и номера кругов в новой записи свои.
    {
        static uint64_t s_session_seen = 0;
        if (ReplaySessionChanged(s_session_seen)) {
            SelectAnalysisLap(-1);
            s_display_vehicle = -1;
        }
    }

    int32_t vehicleId = getDisplayVehicleId();

    // Круги сравнения снимаем ОДИН РАЗ на кадр, до панелей: их читают семь
    // панелей, и каждая копировала бы историю круга заново. После выбора
    // машины — разбираемая сторона зависит от него.
    RefreshReference(vehicleId);

    // Выбор круга живёт, только пока запись стоит: тронулась — панели снова
    // идут за повтором. Проверяем ДО отрисовки панелей, иначе выбор, сделанный
    // щелчком в списке, снимался бы в том же кадре, в котором сделан.
    {
        const telemetry::ReplayStatus replay = telemetry::replay_status();
        if (!replay.active || !replay.paused) SelectAnalysisLap(-1);
    }

    // Сменилась машина — выбранный круг к ней не относится: номера кругов у
    // машин свои, и третий круг одной ничего не говорит о третьем круге другой.
    static int32_t s_last_vehicle = -1;
    if (vehicleId != s_last_vehicle) {
        s_last_vehicle = vehicleId;
        SelectAnalysisLap(-1);
    }

    const float panelTopH = topH; // PRO-статус-бар убран — панели идут сразу под верхним меню

    // ── Global style for all floating panels ─────────────────────────────────
    // NOTE: the app-wide style sets a display-scaled ItemSpacing (huge: thousands
    // of px). Every other UI region overrides it; the pro panels do precise manual
    // layout (DrawList + explicit Dummy gaps) and need it neutralised, otherwise
    // each item/Dummy advances the cursor far below the window and all content is
    // clipped away. Force tight spacing here.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,             (ImVec4)ImColor(COL_BG_PANEL));
    ImGui::PushStyleColor(ImGuiCol_Border,               (ImVec4)ImColor(COL_SEP));
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip,           (ImVec4)ImColor(COL_GOLD_DIM));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered,    (ImVec4)ImColor(COL_GOLD));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,     (ImVec4)ImColor(COL_GOLD));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(18, 18, 18, 255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        IM_COL32(55, 55, 55, 255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(80, 80, 80, 255));

    // Разбираемая машина и машина, чей список кругов открыт, — РАЗНЫЕ вещи, как
    // только оператор закрепил сторону COMPARE. Список остаётся оглавлением
    // записи (по нему ходят за чужим кругом), остальные панели держат
    // закреплённую пару.
    const int32_t analysisId = AnalysisVehicle(vehicleId);

    // Панели рисуются только если включены в боковом меню (McLaren-style).
    if (PanelVisible("LapList"))     RenderLapListWindow    (ctx, vehicleId, sz, panelTopH);
    if (PanelVisible("Channels"))    RenderChannelsWindow   (ctx, analysisId, sz, panelTopH);
    if (PanelVisible("SessionInfo")) RenderSessionInfoWindow(ctx, analysisId, sz, panelTopH);

    if (PanelVisible("TrackMap"))    RenderTrackMapWindow   (ctx, analysisId, sz, panelTopH);
    if (PanelVisible("TrackReport")) RenderTrackReportWindow(ctx, analysisId, sz, panelTopH);
    if (PanelVisible("Relative"))    RenderRelativeWindow   (ctx, analysisId, sz, panelTopH);

    if (PanelVisible("Events"))      RenderEventsWindow     (ctx, sz, panelTopH);
    if (PanelVisible("GForce"))      RenderGForceWindow     (ctx, analysisId, sz, panelTopH);
    if (PanelVisible("GForceLong"))  RenderGForceLongWindow (ctx, analysisId, sz, panelTopH);
    if (PanelVisible("GForceLat"))   RenderGForceLatWindow  (ctx, analysisId, sz, panelTopH);
    if (PanelVisible("Graphs"))      RenderGraphsWindow     (ctx, analysisId, sz, panelTopH);
    if (PanelVisible("Sectors"))     RenderSectorsWindow    (ctx, analysisId, sz, panelTopH);
    if (PanelVisible("Laptime"))     RenderLaptimeWindow    (ctx, analysisId, sz, panelTopH);

    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(5);

    // Боковое меню групп (правый край) — поверх всего, само управляет видимостью.
    RenderSidebar(ctx, sz, panelTopH, botH);

    // Настройки, изменённые в этом кадре, уходят на диск не сразу, а когда
    // оператор перестал их крутить.
    FlushPanelSettings(false);
}

} // namespace Pro
