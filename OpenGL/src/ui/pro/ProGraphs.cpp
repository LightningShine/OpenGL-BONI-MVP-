#include "ProGraphs.h"
#include "../../core/WorldSnapshot.h"
#include "../../network/ReplayPlayer.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>
#include <vector>

extern std::map<int32_t, Vehicle> g_vehicles;

// ============================================================================
// ГРАФИКИ КРУГА (в духе MoTeC i2)
//
// Дорожки одного круга с общей осью X: сверху вниз — скорость, перегрузки,
// ускорение. Ось X — либо дистанция круга (0..100 %), либо время от его начала;
// переключается в шапке.
//
// Показывается ТОЛЬКО ТЕКУЩИЙ круг и только та его часть, что уже проехана: на
// повторе в истории лежит и то, что на этой точке записи ещё не произошло (см.
// visibleSampleCount в Vehicle.h). Ось при этом всегда покрывает круг целиком —
// иначе ползунок нельзя было бы утащить вперёд, в ещё не сыгранную часть.
//
// ПЕРЕМОТКА. Ползунок переставляет позицию повтора простым вычитанием: и таймер
// круга, и позиция в записи идут по меткам пакетов (gps_utc_ms), поэтому
// разница «нужный момент круга минус текущий» РАВНА сдвигу по записи в секундах.
// Никакого отдельного индекса времени заводить не нужно, и никакого расхождения
// между тем, что показывает график, и тем, куда попадёт повтор, быть не может.
// ============================================================================

namespace Pro {
namespace {

// Ось X: по дистанции круга или по времени от его начала. Дистанция — по
// умолчанию: на ней один и тот же поворот всегда в одном и том же месте оси,
// поэтому два круга сравнимы на глаз, а ось не растёт по ходу круга.
enum class Axis { Distance, Time };

struct Sample
{
    float    time = 0.f;    // секунды от начала круга
    float    dist = 0.f;    // доля круга 0..1
    float    speed = 0.f;   // км/ч
    float    accel = 0.f;   // м/с²
    float    g_long = 0.f;  // продольная перегрузка, g (плюс — разгон)
    float    g_lat = 0.f;   // поперечная перегрузка, g
    uint32_t utc = 0;       // метка источника — абсолютный адрес момента в записи

    // ОТСТАВАНИЕ ОТ ОБРАЗЦА в этой точке круга, секунды (плюс — медленнее).
    //
    // Считается по дистанции, а не по времени: «на этом месте трассы я был на
    // 0.31 с позже» — единственная форма, в которой сравнение двух кругов
    // отвечает на вопрос ГДЕ потеряно время. Разность времён кругов говорит
    // только СКОЛЬКО, а этого мало.
    float    delta = 0.f;
    bool     has_delta = false;
};

enum class ChannelId { Speed, GLong, GLat, GSum, Brake, Accel, Delta };

struct Channel
{
    ChannelId   id;
    const char* key;        // ключ персиста видимости дорожки
    const char* label;
    const char* unit;
    const char* format;     // формат значения, без единицы
    ImU32       color;
    bool        bipolar;    // шкала симметрична относительно нуля
    float       min_span;   // минимальный размах шкалы, в единицах канала
    float       span_step;  // шаг округления шкалы
};

// Каналы — только те, что реально приходят с трекера, плюс ОДИН производный
// (BRAKE). Своей педали тормоза у нас нет, и рисовать её «как в MoTeC» значило
// бы показать инженеру число, которого никто не измерял. Замедление же
// измерено: продольная перегрузка со знаком минус — это и есть торможение,
// ровно тем же способом её показывает панель G-FORCE LONG.
const Channel CHANNELS[] = {
    { ChannelId::Speed, "Gr.Speed", "SPEED",  "km/h",      "%.1f",  COL_GOLD,                   false, 40.f, 10.f },
    { ChannelId::GLong, "Gr.GLong", "G LONG", "g",         "%+.2f", COL_GREEN,                  true,   1.f,  0.5f },
    { ChannelId::GLat,  "Gr.GLat",  "G LAT",  "g",         "%+.2f", COL_CYAN,                   true,   1.f,  0.5f },
    { ChannelId::GSum,  "Gr.GSum",  "G SUM",  "g",         "%.2f",  IM_COL32(190, 140, 235, 255), false, 1.f,  0.5f },
    { ChannelId::Brake, "Gr.Brake", "BRAKE",  "g",         "%.2f",  COL_RED,                    false,  1.f,  0.5f },
    { ChannelId::Accel, "Gr.Accel", "ACCEL",  "m/s\xc2\xb2", "%+.1f", IM_COL32(235, 195, 100, 255), true, 4.f,  2.f },
    // Дорожка сравнения. Рисуется, только когда выбран круг-образец: без него
    // ей неоткуда взяться, и пустая полоса на полэкрана только мешала бы.
    { ChannelId::Delta, "Gr.Delta", "DELTA",  "s",         "%+.3f", IM_COL32(0xDA, 0xA5, 0x40, 255), true, 0.5f, 0.25f },
};
float channel_value(ChannelId id, const Sample& s)
{
    switch (id) {
        case ChannelId::Speed: return s.speed;
        case ChannelId::GLong: return s.g_long;
        case ChannelId::GLat:  return s.g_lat;
        case ChannelId::GSum:  return sqrtf(s.g_long * s.g_long + s.g_lat * s.g_lat);
        case ChannelId::Brake: return s.g_long < 0.f ? -s.g_long : 0.f;
        case ChannelId::Accel: return s.accel;
        case ChannelId::Delta: return s.delta;
    }
    return 0.f;
}

// Разрыв в данных: между соседними сэмплами прошло больше — линию рвём.
// История пишется по ходу заезда, а перемотка ВПЕРЁД проносит запись мимо:
// между двумя соседними точками оказывается весь промотанный участок, и
// соединять их линией — значит нарисовать езду, которой не было.
constexpr float MAX_GAP_SECONDS = 0.6f;
constexpr float MAX_GAP_DIST    = 0.05f;

// Самое узкое окно просмотра — 2 % круга. Дальше приближать нечего: сэмплы
// идут десять раз в секунду, и на таком масштабе между соседними точками уже
// полполотна.
constexpr float MIN_VIEW_SPAN = 0.02f;

// Запас в правом нижнем углу под уголок изменения размера окна: наша область
// перехвата мыши не должна его закрывать, иначе панель нельзя растянуть.
constexpr float RESIZE_GRIP_PT = 20.f;

/// Округлённый вверх размах шкалы. Скачущая шкала обесценивает график: два
/// кадра подряд нельзя сравнить на глаз, а именно за этим на него и смотрят.
/// Поэтому размах не следует за данными плотно, а прыгает шагами.
float nice_span(float max_abs, float step, float min_span)
{
    const float span = ceilf(max_abs / step) * step;
    return span < min_span ? min_span : span;
}

/// Данные одного круга, снятые под мьютексом машин и дальше живущие сами:
/// держать лок на всю отрисовку нельзя, его и так ждёт приём телеметрии.
struct LapTrace
{
    std::vector<Sample> samples;     // текущий круг целиком
    float now_time = 0.f;            // секунды текущего круга
    float now_dist = 0.f;            // доля круга 0..1
    float ref_lap_time = 0.f;        // длительность круга-образца, с; 0 — нет
    float lap_time = 0.f;            // записанное время самого круга, с; 0 — нет
    int   lap_number = 0;
    bool  found = false;
    // Ползунок показывается, только если запись СТОИТ ВНУТРИ этого круга.
    // Разбирая круг, на котором записи нет, ставить «где мы сейчас» некуда, а
    // ползунок в нуле читался бы как «машина на старт/финише».
    bool  at_playhead = false;
};

Sample to_sample(const LapInfo& info)
{
    Sample s;
    s.time   = info.timefromstart;
    s.dist   = static_cast<float>(info.progress);
    s.speed  = info.speed;
    s.accel  = info.aceleration;
    s.g_long = info.gForceY;
    s.g_lat  = info.gForceX;
    s.utc    = info.utc_ms;
    return s;
}

void copy_samples(const std::vector<LapInfo>& src, size_t count, std::vector<Sample>& out)
{
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
        out.push_back(to_sample(src[i]));
}

/// Переводит готовый образец (Pro::Reference) в дорожку графика и считает
/// отставание разбираемого круга от него в каждой точке.
///
/// Дорожка образца строится из ТЕХ ЖЕ замеров, что показывают остальные панели:
/// второго чтения истории нет, копия снята один раз за кадр в ProView.
void apply_reference(LapTrace& lap, std::vector<Sample>& ref_out)
{
    ref_out.clear();
    const RefTrace& reference = Reference();
    if (!reference.valid) return;

    ref_out.reserve(reference.samples.size());
    for (const LapInfo& info : reference.samples)
        ref_out.push_back(to_sample(info));

    LapInfo at;
    for (Sample& s : lap.samples) {
        if (!reference.at(s.dist, at)) continue;
        s.delta     = s.time - at.timefromstart;
        s.has_delta = true;
    }
}

/// Снимает историю круга, выбранного для разбора.
LapTrace read_lap(int32_t vehicleId)
{
    LapTrace lap;

    // Какой круг показывать, спрашиваем ДО захвата мьютекса машин: AnalysisLap
    // ходит в RaceManager, а тот берёт тот же мьютекс.
    const int analysis_lap = AnalysisLap(vehicleId);

    VehiclesLock lk;
    const auto it = g_vehicles.find(vehicleId);
    if (it == g_vehicles.end()) return lap;

    const Vehicle& v = it->second;
    lap.found      = true;
    lap.lap_number = (analysis_lap >= 0) ? analysis_lap : v.m_current_lap_number;
    lap.at_playhead = (lap.lap_number == v.m_current_lap_number);
    // Момент берём у ТОЙ ЖЕ машины, чью историю читаем: по её m_track_progress
    // отрезается видимая часть круга, и ползунок обязан стоять ровно на срезе.
    lap.now_time = lap.at_playhead ? v.m_current_lap_timer : 0.f;
    lap.now_dist = lap.at_playhead ? static_cast<float>(v.m_track_progress) : 0.f;

    // Круг показывается ЦЕЛИКОМ, а не по точку просмотра.
    //
    // На повторе телеметрию берём из ЖУРНАЛА ЗАПИСИ: он построен при открытии
    // файла, полон с первой секунды и перемоткой не задевается. Иначе только
    // что начавшийся круг выглядел бы пустым, хотя в файле он есть весь, а
    // ползунок нельзя было бы утащить туда, где ещё не были. Живой заезд читает
    // историю самой машины — там будущего просто не существует.
    const std::shared_ptr<const telemetry::VehicleJournal> journal =
        telemetry::replay_journal(vehicleId);

    if (journal != nullptr) {
        if (const auto current = journal->lap_samples.find(lap.lap_number);
            current != journal->lap_samples.end())
            copy_samples(current->second, current->second.size(), lap.samples);

        if (const auto self = journal->lap_times.find(lap.lap_number);
            self != journal->lap_times.end())
            lap.lap_time = self->second.lapTime;

        if (const auto previous = journal->lap_times.find(lap.lap_number - 1);
            previous != journal->lap_times.end())
            lap.ref_lap_time = previous->second.lapTime;
        else if (journal->best_lap_time > 0.f)
            lap.ref_lap_time = journal->best_lap_time;

        return lap;
    }

    if (const auto current = v.laps.find(lap.lap_number); current != v.laps.end())
        copy_samples(current->second.samples, current->second.samples.size(), lap.samples);

    // Круг-образец — предыдущий: он ближе всего по темпу и по трафику. Нет его
    // (первый круг после старта) — берём лучший, он хотя бы есть.
    if (const auto previous = v.m_laps.find(v.m_current_lap_number - 1);
        previous != v.m_laps.end())
        lap.ref_lap_time = previous->second.lapTime;
    else if (v.m_best_lap_time > 0.f)
        lap.ref_lap_time = v.m_best_lap_time;

    return lap;
}

/// Замер, ближайший к точке `x` полотна. nullptr — истории нет.
///
/// Один поиск и на подпись значения, и на перемотку: то и другое отвечает на
/// один и тот же вопрос — «какой замер вот здесь».
const Sample* sample_at_x(const std::vector<Sample>& samples, float x,
                          const std::function<float(const Sample&)>& x_of)
{
    const Sample* best = nullptr;
    float best_distance = FLT_MAX;
    for (const Sample& s : samples) {
        const float d = fabsf(x_of(s) - x);
        if (d < best_distance) { best_distance = d; best = &s; }
    }
    return best;
}

/// Ломаная одной дорожки: копится по точкам и выливается на разрыве данных.
class TraceBuilder
{
public:
    TraceBuilder(ImDrawList* draw_list, ImU32 color, float thickness)
        : dl_(draw_list), color_(color), thickness_(thickness) {}

    void add(ImVec2 point) { points_.push_back(point); }

    void flush()
    {
        if (points_.size() >= 2)
            dl_->AddPolyline(points_.data(), static_cast<int>(points_.size()), color_,
                             ImDrawFlags_None, thickness_);
        else if (points_.size() == 1)
            dl_->AddCircleFilled(points_[0], thickness_ * 0.7f, color_, 0);
        points_.clear();
    }

private:
    ImDrawList*         dl_;
    ImU32               color_;
    float               thickness_;
    std::vector<ImVec2> points_;
};

}  // namespace

void RenderGraphsWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH)
{
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({260.f * ui, topH + 590.f * ui}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({820.f * ui, 400.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({260.f * ui, 140.f * ui}, {vpSz.x, vpSz.y});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG));
    if (!ImGui::Begin("##Graphs", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    const float w = ImGui::GetWindowWidth();
    const float z = PanelZoom("Graphs");
    DrawPanelHeader(ctx, "GRAPHS", false, "Graphs");

    ImDrawList*  dl    = ImGui::GetWindowDrawList();
    const ImVec2 wp    = ImGui::GetWindowPos();
    const ImVec2 base  = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    // ── Переключатели в шапке ────────────────────────────────────────────────
    static Axis s_axis = Axis::Distance;

    // Окно просмотра по оси X — в долях ПОЛНОЙ оси круга. Это и есть
    // приближение: график всегда рисует круг целиком, а показывает из него
    // вырезанное окно.
    static float s_view_min  = 0.f;
    static float s_view_span = 1.f;

    // Приближение относится к ТОЙ записи, на которой его выставили. Открылась
    // другая — возвращаем полотно к целому кругу: чужое окно просмотра на новой
    // записи показывает случайный кусок, и понять, что он случайный, нельзя.
    {
        static uint64_t s_session_seen = 0;
        if (ReplaySessionChanged(s_session_seen)) { s_view_min = 0.f; s_view_span = 1.f; }
    }

    const float hgap = ui_scale::points(28.f);   // место под крестик закрытия

    bool axisClicked = false;
    const float axisX = HeaderButton(ctx, s_axis == Axis::Time ? "TIME" : "DIST", true, false,
                                     wp.x + w - hgap, axisClicked);
    if (axisClicked) s_axis = (s_axis == Axis::Time) ? Axis::Distance : Axis::Time;

    bool channelsClicked = false;
    const float channelsX = HeaderButton(ctx, "CHANNELS", false, /*arrow=*/true,
                                         axisX - ui_scale::points(14.f), channelsClicked);
    if (channelsClicked) ImGui::OpenPopup("##graphChannels");

    float nextX = channelsX;

    // Кнопка «весь круг» — только когда график приближен: у неё есть смысл
    // ровно тогда, когда есть что возвращать на место.
    if (s_view_span < 1.f) {
        bool fitClicked = false;
        nextX = HeaderButton(ctx, "FIT", true, false, nextX - ui_scale::points(14.f), fitClicked);
        if (fitClicked) { s_view_min = 0.f; s_view_span = 1.f; }
    }

    // Выход из сравнения. Квадратики снимаются только со своей строки, то есть
    // из списка кругов того гонщика, у которого стоят; уйдя к третьему
    // участнику, оператор оказывался заперт в сравнении, следов которого на его
    // экране уже нет. Здесь оно снимается откуда угодно.
    if (ComparisonActive()) {
        bool clearClicked = false;
        HeaderButton(ctx, "CLEAR", true, false, nextX - ui_scale::points(14.f), clearClicked);
        if (clearClicked) ClearComparison();
    }

    ImGui::SetNextWindowPos({channelsX - ui_scale::points(8.f), wp.y + header_h()});
    PushDropdownStyle();
    if (ImGui::BeginPopup("##graphChannels")) {
        if (ctx.regular) ImGui::PushFont(ctx.regular);
        for (const Channel& ch : CHANNELS)
            if (ImGui::MenuItem(ch.label, nullptr, PanelVisible(ch.key)))
                TogglePanel(ch.key);
        if (ctx.regular) ImGui::PopFont();
        ImGui::EndPopup();
    }
    PopDropdownStyle();

    if (avail.x < 16.f || avail.y < 16.f) { ImGui::End(); ImGui::PopStyleColor(); return; }

    // ── Раскладка ────────────────────────────────────────────────────────────
    // Значения — жирным Ubuntu, как на шкалах перегрузки: цифра и есть ответ,
    // подпись рядом с ней только поясняет. Моноширинный из ProContext сюда не
    // годится — он загружен кеглем заголовка (FONT_PT_TITLE) и в строку дорожки
    // не помещается.
    ImFont*     lf  = ctx.russo ? ctx.russo : ImGui::GetFont();
    ImFont*     vf  = ctx.bold  ? ctx.bold  : lf;
    const float lSz = lf->FontSize * z;
    const float vSz = vf->FontSize * z;
    const float pad = pad_px() * 0.6f;

    const telemetry::ReplayStatus replay = telemetry::replay_status();

    const float axisH  = lSz + pad;
    const float scrubH = replay.active ? ui_scale::points(18.f) : 0.f;
    const float plotH  = avail.y - axisH - scrubH;
    if (plotH < 20.f) { ImGui::End(); ImGui::PopStyleColor(); return; }

    const ImVec2 plotMin{base.x + pad, base.y};
    const ImVec2 plotMax{base.x + avail.x - pad, base.y + plotH};
    const float  plotW = plotMax.x - plotMin.x;
    if (plotW < 8.f) { ImGui::End(); ImGui::PopStyleColor(); return; }

    // ── Перехват мыши ────────────────────────────────────────────────────────
    // Именно кнопкой, а не проверкой прямоугольника: окно PRO-панели не имеет
    // заголовка, и ImGui двигает такое окно за любое пустое место. Без своего
    // элемента перетаскивание ползунка таскало бы саму панель.
    // Перемотка — РОВНО ОДНА на нажатие (IsItemClicked), а не каждый кадр, пока
    // кнопка нажата. Пока запрос повторялся кадр за кадром, удержание уносило
    // запись вперёд: следующий запрос уходил раньше, чем успевал примениться
    // предыдущий, и сдвиги складывались. Нажал — попал в точку; держишь —
    // ничего не происходит.
    ImGui::SetCursorScreenPos(base);
    ImGui::InvisibleButton("##graphPlot", {avail.x, plotH});
    const bool plotHovered = ImGui::IsItemHovered();
    const bool plotActive  = ImGui::IsItemActive();
    const bool plotClicked = ImGui::IsItemClicked();

    bool stripClicked = false;
    bool stripActive  = false;
    if (replay.active) {
        // Полосу обрываем перед уголком изменения размера окна — иначе панель
        // нельзя растянуть за правый нижний угол.
        const float stripW = fmaxf(avail.x - ui_scale::points(RESIZE_GRIP_PT), 1.f);
        ImGui::SetCursorScreenPos({base.x, base.y + plotH + axisH});
        ImGui::InvisibleButton("##graphScrub", {stripW, fmaxf(scrubH, 1.f)});
        stripClicked = ImGui::IsItemClicked();
        stripActive  = ImGui::IsItemActive();
        if (ImGui::IsItemHovered() || plotHovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else {
        ImGui::SetCursorScreenPos({base.x, base.y + plotH});
        ImGui::Dummy({avail.x, axisH});
    }
    const bool dragging = plotActive || stripActive;
    const bool clicked  = plotClicked || stripClicked;

    LapTrace lap = read_lap(vehicleId);

    // ── Сторона сравнения ────────────────────────────────────────────────────
    // Белая кривая поверх цветной — это круг-образец (REF). Отставание от него
    // считается здесь же и уходит в дорожку DELTA.
    // Статический буфер: круг-образец меняется редко, а вектор на несколько
    // тысяч замеров переаллоцировался бы каждый кадр. clear() держит ёмкость.
    // Панель одна и рисуется в одном потоке, поэтому общее состояние безопасно.
    static std::vector<Sample> ref_samples;
    apply_reference(lap, ref_samples);
    const RefTrace& reference   = Reference();
    const bool      comparing   = ComparisonActive() && !ref_samples.empty();

    // ── Ось X ────────────────────────────────────────────────────────────────
    // По времени ось покрывает круг ЦЕЛИКОМ: и то, что уже записано, и круг-
    // образец на случай, когда круг только начался. Иначе ось росла бы вместе с
    // кругом — ползунок некуда тащить вперёд, — а на повторе, где круг уже
    // проеден весь, его хвост уезжал бы за правый край.
    // Ось по времени обязана вместить и круг-образец: он бывает длиннее
    // разбираемого, и без запаса его хвост уезжал бы за правый край.
    const float ref_axis_time =
        comparing ? fmaxf(reference.lap_time, ref_samples.back().time) : 0.f;
    const float time_span = nice_span(
        fmaxf(fmaxf(fmaxf(lap.now_time, lap.ref_lap_time), lap.lap_time),
              fmaxf(ref_axis_time, lap.samples.empty() ? 0.f : lap.samples.back().time)),
        5.f, 15.f);

    // Положение на ПОЛНОЙ оси круга (0..1) — общая мера для графика, обзорной
    // полосы и перемотки. Приближение живёт отдельно, в окне просмотра.
    auto axis_of = [&](float time, float dist) {
        return (s_axis == Axis::Time) ? (time_span > 0.f ? time / time_span : 0.f) : dist;
    };
    auto x_of = [&](float time, float dist) {
        return plotMin.x + (axis_of(time, dist) - s_view_min) / s_view_span * plotW;
    };
    const std::function<float(const Sample&)> sample_x =
        [&](const Sample& s) { return x_of(s.time, s.dist); };
    // Обзорная полоса всегда показывает круг целиком, независимо от приближения:
    // приближенный график — это лупа, а прыгать по кругу нужно и из-под лупы.
    auto strip_x_of = [&](float axis01) { return plotMin.x + axis01 * plotW; };

    // ── Приближение и сдвиг окна просмотра ───────────────────────────────────
    // Колесо        — приближение вокруг курсора;
    // Shift+колесо  — сдвиг вбок (мышью);
    // два пальца по тачпаду вбок — то же самое (горизонтальное колесо);
    // правая кнопка — потянуть окно, двойной правый клик — вернуть круг целиком.
    // Ctrl+колесо занято зумом текста панели (PanelZoom) и сюда не доходит.
    //
    // На неприближенном графике сдвигать нечего: окно и так во весь круг, и
    // клэмп ниже возвращает его на место сам.
    ImGuiIO& io = ImGui::GetIO();
    if (plotHovered && io.MouseWheel != 0.f && !io.KeyCtrl && !io.KeyShift) {
        // Точка под курсором остаётся на месте — иначе приближаешь одно, а
        // получаешь другое, и попасть в нужный поворот невозможно.
        const float anchor = s_view_min +
                             std::clamp((io.MousePos.x - plotMin.x) / plotW, 0.f, 1.f) * s_view_span;
        const float before = s_view_span;
        s_view_span = std::clamp(before * powf(0.85f, io.MouseWheel), MIN_VIEW_SPAN, 1.f);
        s_view_min  = anchor - (anchor - s_view_min) * (s_view_span / before);
    }
    if (plotHovered && s_view_span < 1.f) {
        // Шаг — доля ВИДИМОГО окна: на сильном приближении сдвиг обязан быть
        // мелким, иначе один щелчок колеса проносит мимо всего поворота.
        constexpr float PAN_PER_NOTCH = 0.2f;
        float pan = 0.f;
        if (io.MouseWheelH != 0.f)                pan += io.MouseWheelH;
        if (io.KeyShift && io.MouseWheel != 0.f)  pan -= io.MouseWheel;
        s_view_min += pan * PAN_PER_NOTCH * s_view_span;

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            s_view_min -= io.MouseDelta.x / plotW * s_view_span;
    }
    if (plotHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right)) {
        s_view_min = 0.f;
        s_view_span = 1.f;
    }

    // Приближенное окно едет за точкой просмотра, но только когда та САМА
    // поехала: иначе окно, отведённое в сторону на паузе, тут же дёргало бы
    // обратно и разглядеть соседний поворот было бы нельзя.
    const float head01 = axis_of(lap.now_time, lap.now_dist);
    static float s_prev_head = 0.f;
    const bool   head_moved  = fabsf(head01 - s_prev_head) > 1e-4f;
    s_prev_head = head01;
    if (head_moved && !dragging && (head01 < s_view_min || head01 > s_view_min + s_view_span))
        s_view_min = head01 - s_view_span * 0.5f;

    s_view_min = std::clamp(s_view_min, 0.f, 1.f - s_view_span);

    // Ползунок: где мы сейчас в круге. Нужен и отрисовке, и перемотке — на нём
    // стоит проверка захвата, поэтому считаем его до того и другого.
    // Время в точке оси. По оси времени это она сама; по оси дистанции время
    // берём у ближайшего замера круга — подписи концов и центра везде в
    // СЕКУНДАХ, потому что доля круга ни о чём не говорит: «75 %» не отвечает
    // ни на один вопрос, который задают графику.
    auto time_at_axis = [&](float axis01) -> float {
        if (s_axis == Axis::Time) return axis01 * time_span;
        const Sample* best = nullptr;
        float best_distance = FLT_MAX;
        for (const Sample& sample : lap.samples) {
            const float d = fabsf(sample.dist - axis01);
            if (d < best_distance) { best_distance = d; best = &sample; }
        }
        return best != nullptr ? best->time : 0.f;
    };

    const float headX      = plotMin.x + (head01 - s_view_min) / s_view_span * plotW;
    const float stripHeadX = strip_x_of(head01);
    const float cursorX    = ImGui::GetIO().MousePos.x;

    // ── Перемотка мышью ──────────────────────────────────────────────────────
    // Щелчок по графику — ОДИН прыжок в эту точку, вперёд или назад, и на этом
    // всё: удержание больше ничего не делает. Чтобы тащить, ползунок надо взять
    // — нажать на саму жёлтую линию (или на полосу внизу, она вся ползунок).
    static bool s_head_grabbed = false;

    if (replay.active && lap.found && !lap.samples.empty()) {
        const float grab_px = ui_scale::points(7.f);

        if (plotClicked)       s_head_grabbed = fabsf(cursorX - headX) <= grab_px;
        else if (stripClicked) s_head_grabbed = true;
        if (!plotActive && !stripActive) s_head_grabbed = false;

        if (clicked || (s_head_grabbed && dragging)) {
            // Целимся в замер под курсором и перематываем на ЕГО метку времени.
            // Адрес абсолютный, поэтому щелчок по одному и тому же месту всегда
            // приводит в одно и то же место — сколько раз ни щёлкни.
            //
            // По полосе метят в круг целиком, по графику — внутрь видимого окна.
            const float x01    = std::clamp((cursorX - plotMin.x) / plotW, 0.f, 1.f);
            const float axis01 = stripActive ? x01 : s_view_min + x01 * s_view_span;
            const float target_x = plotMin.x + (axis01 - s_view_min) / s_view_span * plotW;

            if (const Sample* target = sample_at_x(lap.samples, target_x, sample_x))
                if (target->utc != 0)
                    telemetry::replay_seek_to_utc(target->utc);
        }
    } else {
        s_head_grabbed = false;
    }

    dl->AddRectFilled(base, {base.x + avail.x, base.y + plotH}, COL_BG_WIDGET);
    // Приближенный график заведомо шире окна — всё лишнее режем по полотну,
    // иначе кривые уезжают на подписи оси и на соседние панели.
    dl->PushClipRect(base, {base.x + avail.x, base.y + plotH}, true);

    // ── Дорожки ──────────────────────────────────────────────────────────────
    // Дорожка DELTA существует только при выбранном образце: без него у неё нет
    // данных, а полоса всё равно съедала бы высоту у остальных.
    const auto lane_enabled = [&](const Channel& ch) {
        if (!PanelVisible(ch.key)) return false;
        return ch.id != ChannelId::Delta || comparing;
    };

    int shown = 0;
    for (const Channel& ch : CHANNELS)
        if (lane_enabled(ch)) ++shown;

    // Курсор мыши: под ним показывается значение из истории, а не текущее —
    // график для того и нужен, чтобы посмотреть, что было в другой момент.
    const bool hasCursor = plotHovered && !lap.samples.empty();

    if (shown > 0) {
        const float laneH  = plotH / shown;
        const float rowH   = fmaxf(lSz, vSz) + 2.f;
        const bool  labels = laneH > rowH * 2.2f;

        int lane = 0;
        for (const Channel& ch : CHANNELS) {
            if (!lane_enabled(ch)) continue;

            // На дорожке DELTA образца нет: она сама и есть сравнение.
            const bool lane_has_ref = comparing && ch.id != ChannelId::Delta;

            const float top = plotMin.y + lane * laneH;
            const float bot = top + laneH;
            ++lane;

            if (lane > 1)
                dl->AddLine({base.x, top}, {base.x + avail.x, top}, COL_SEP, 1.f);

            const float bandTop = top + (labels ? rowH : 2.f);
            const float bandBot = bot - 2.f;
            const float bandH   = bandBot - bandTop;
            if (bandH < 2.f) continue;

            // Шкала ОБЩАЯ на обе кривые: разные шкалы делают сравнение
            // бессмысленным — кривые совпадали бы на глаз при разных величинах.
            float peak = 0.f;
            for (const Sample& s : lap.samples)
                peak = fmaxf(peak, fabsf(channel_value(ch.id, s)));
            if (lane_has_ref)
                for (const Sample& s : ref_samples)
                    peak = fmaxf(peak, fabsf(channel_value(ch.id, s)));
            const float span = nice_span(peak, ch.span_step, ch.min_span);

            auto y_of = [&](float value) {
                const float v01 = ch.bipolar ? (0.5f - value / (2.f * span))
                                             : (1.f - value / span);
                return bandTop + std::clamp(v01, 0.f, 1.f) * bandH;
            };

            // Базовая линия: ноль у знаковых каналов, низ шкалы у остальных.
            const float zeroY = ch.bipolar ? y_of(0.f) : bandBot;
            dl->AddLine({plotMin.x, zeroY}, {plotMax.x, zeroY}, IM_COL32(58, 58, 58, 255), 1.f);

            // Образец рисуем ПЕРВЫМ, но той же толщины и в полную силу цвета:
            // тонкая полупрозрачная линия терялась на дорожке, а её для того и
            // рисуют, чтобы видеть. Поверх ложится разбираемый круг — он
            // главный на экране, и перекрывать его белым нельзя.
            if (lane_has_ref) {
                TraceBuilder ref_trace(dl, COL_REF, fmaxf(1.4f * z, 1.f));
                for (size_t i = 0; i < ref_samples.size(); ++i) {
                    const Sample& s = ref_samples[i];
                    if (i > 0) {
                        const Sample& prev = ref_samples[i - 1];
                        if (s.time - prev.time > MAX_GAP_SECONDS ||
                            s.dist - prev.dist > MAX_GAP_DIST)
                            ref_trace.flush();
                    }
                    ref_trace.add({x_of(s.time, s.dist), y_of(channel_value(ch.id, s))});
                }
                ref_trace.flush();
            }

            // Кривая
            TraceBuilder trace(dl, ch.color, fmaxf(1.4f * z, 1.f));
            for (size_t i = 0; i < lap.samples.size(); ++i) {
                const Sample& s = lap.samples[i];
                // Там, где образец круг не покрывает, отставание не измерено —
                // и линию туда вести нельзя: ноль читался бы как «шли вровень».
                if (ch.id == ChannelId::Delta && !s.has_delta) { trace.flush(); continue; }
                if (i > 0) {
                    const Sample& p = lap.samples[i - 1];
                    if (s.time - p.time > MAX_GAP_SECONDS || s.dist - p.dist > MAX_GAP_DIST)
                        trace.flush();
                }
                trace.add({x_of(s.time, s.dist), y_of(channel_value(ch.id, s))});
            }
            trace.flush();

            if (!labels) continue;

            // Значение — в точке курсора, а без курсора в ТОЧКЕ ВОСПРОИЗВЕДЕНИЯ.
            //
            // Не в конце круга: круг рисуется целиком, и последний его замер —
            // это финиш, а не «сейчас». Числа стояли на месте при идущем повторе
            // именно поэтому. Единица измерения в подписи всегда: «-0.42» без
            // «g» ничего не значит.
            char value_text[40] = "--";
            char ref_text[48]    = "";
            const float readX = hasCursor ? cursorX : headX;

            if (const Sample* at = sample_at_x(lap.samples, readX, sample_x)) {
                float shown_value = channel_value(ch.id, *at);

                // Отставание в ТОЧКЕ ПРОСМОТРА берём из общей точки — той же,
                // из которой его читает LAPTIME. Ближайший замер отстоит от
                // точки просмотра на шаг выборки, и два числа на соседних
                // панелях расходились ровно на него. Под курсором показываем
                // сам замер: там разбирают конкретную точку кривой.
                if (ch.id == ChannelId::Delta && !hasCursor) {
                    float live = 0.f;
                    if (ComparisonDelta(vehicleId, live)) shown_value = live;
                }

                char number[24];
                snprintf(number, sizeof(number), ch.format, shown_value);
                snprintf(value_text, sizeof(value_text), "%s %s", number, ch.unit);

                // Рядом со значением разбираемого круга — то же место образца и
                // разница между ними. Без этих двух чисел белая кривая говорит
                // «где-то тут медленнее», но не говорит НАСКОЛЬКО.
                if (lane_has_ref) {
                    if (const Sample* ref_at = sample_at_x(ref_samples, readX, sample_x)) {
                        const float mine  = channel_value(ch.id, *at);
                        const float other = channel_value(ch.id, *ref_at);

                        // Разница — ОБРАЗЕЦ МИНУС СВОЁ, а не наоборот. Она стоит
                        // рядом со значением образца и написана его цветом,
                        // поэтому и говорить обязана про него: «REF 42.1 +9.6» —
                        // у образца здесь на 9.6 больше. Обратный знак при том же
                        // цвете читался ровно наоборот.
                        char ref_number[24], delta_number[24];
                        snprintf(ref_number,   sizeof(ref_number),   ch.format, other);
                        snprintf(delta_number, sizeof(delta_number), "%+.2f", other - mine);
                        snprintf(ref_text, sizeof(ref_text), "REF %s   %s",
                                 ref_number, delta_number);
                    }
                }
            }

            // Подпись и значение центрируем в строке КАЖДОЕ по своему кеглю:
            // шрифты разные, и от общей верхней кромки они встают ступенькой.
            const float labelY = top + (rowH - lSz) * 0.5f;
            const float valueY = top + (rowH - vSz) * 0.5f;

            dl->AddText(lf, lSz, {plotMin.x + 2.f, labelY}, COL_LABEL, ch.label);
            const float labelW = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, ch.label).x;
            const float valueX = plotMin.x + 2.f + labelW + pad;
            dl->AddText(vf, vSz, {valueX, valueY}, ch.color, value_text);

            if (ref_text[0] != '\0') {
                const float vw = vf->CalcTextSizeA(vSz, FLT_MAX, 0.f, value_text).x;
                dl->AddText(lf, lSz, {valueX + vw + pad, labelY}, COL_REF, ref_text);
            }

            // Верх шкалы у правого края — чтобы величину можно было прикинуть,
            // не наводя курсор.
            char span_text[24];
            snprintf(span_text, sizeof(span_text), ch.bipolar ? "+/-%.1f" : "%.1f", span);
            const float spanW = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, span_text).x;
            dl->AddText(lf, lSz, {plotMax.x - spanW - 2.f, labelY}, COL_LABEL, span_text);
        }
    } else {
        const char* none = "no channels selected";
        const float nw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, none).x;
        dl->AddText(lf, lSz, {base.x + (avail.x - nw) * 0.5f, base.y + (plotH - lSz) * 0.5f},
                    COL_DIM, none);
    }

    // Пустой график и выключенные дорожки — разные причины пустого окна, и
    // сообщение о каждой своё. Обе разом писать некуда: они встали бы одно на
    // другое посреди панели.
    if (shown > 0 && lap.samples.empty()) {
        const char* msg = lap.found ? "waiting for the lap to start" : "no vehicle selected";
        const float mw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, msg).x;
        dl->AddText(lf, lSz, {base.x + (avail.x - mw) * 0.5f, base.y + plotH * 0.5f},
                    COL_DIM, msg);
    }

    // ── Курсор мыши поверх дорожек ───────────────────────────────────────────
    if (hasCursor) {
        const float cx = std::clamp(cursorX, plotMin.x, plotMax.x);
        dl->AddLine({cx, plotMin.y}, {cx, plotMax.y}, IM_COL32(255, 255, 255, 70), 1.f);
    }

    // ── Ползунок: где мы сейчас в круге ──────────────────────────────────────
    if (lap.found && lap.at_playhead) {
        dl->AddLine({headX, plotMin.y}, {headX, plotMax.y}, COL_GOLD, fmaxf(1.5f * z, 1.f));
        const float th = ui_scale::points(5.f);
        dl->AddTriangleFilled({headX - th, plotMin.y}, {headX + th, plotMin.y},
                              {headX, plotMin.y + th * 1.6f}, COL_GOLD);
    }

    dl->PopClipRect();

    // ── Подписи оси ──────────────────────────────────────────────────────────
    // Концы оси — это концы ВИДИМОГО окна, а не круга: приблизив график, надо
    // видеть, какой участок остался на экране.
    {
        const float ty = plotMax.y + (axisH - lSz) * 0.5f;
        char left[24], right[24], middle[160];

        const float view_hi = s_view_min + s_view_span;
        snprintf(left,  sizeof(left),  "%.1f s", time_at_axis(s_view_min));
        snprintf(right, sizeof(right), "%.1f s", time_at_axis(view_hi));

        // Внутри круга подпись показывает точку записи, снаружи — время самого
        // круга: ноль на круге, где записи нет, читался бы как позиция.
        char where[48];
        if (lap.at_playhead)
            snprintf(where, sizeof(where), "%.2f s", lap.now_time);
        else if (lap.lap_time > 0.f)
            snprintf(where, sizeof(where), "%.3f s", lap.lap_time);
        else
            snprintf(where, sizeof(where), "---");

        if (s_view_span < 1.f)
            snprintf(middle, sizeof(middle), "LAP %d   %s   x%.1f",
                     lap.lap_number, where, 1.f / s_view_span);
        else
            snprintf(middle, sizeof(middle), "LAP %d   %s", lap.lap_number, where);

        const float lw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, left).x;
        const float rw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, right).x;

        const float mw = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, middle).x;

        if (comparing) {
            // ── Кто с кем ────────────────────────────────────────────────────
            // Каждая сторона написана СВОИМ цветом: белым — образец, золотым —
            // разбираемый круг. Те же два цвета на всех панелях, поэтому
            // спрашивать «а слева чьё?» не приходится.
            const std::shared_ptr<const world::Snapshot> snapshot = world::current();
            const world::VehicleView* self = world::find(*snapshot, vehicleId);

            char selfName[32];
            if (self != nullptr && !self->name.empty() && self->name != "Unknown")
                snprintf(selfName, sizeof(selfName), "%s", self->name.c_str());
            else
                snprintf(selfName, sizeof(selfName), "CAR %d", vehicleId);

            // Порядок как у чисел на всех панелях: СВОЁ, потом ОБРАЗЕЦ. Обратный
            // заставлял держать в голове, какая сторона где.
            char cmpPart[80], refPart[80];
            if (s_view_span < 1.f)
                snprintf(cmpPart, sizeof(cmpPart), "%s LAP %d   x%.1f",
                         selfName, lap.lap_number, 1.f / s_view_span);
            else
                snprintf(cmpPart, sizeof(cmpPart), "%s LAP %d", selfName, lap.lap_number);
            snprintf(refPart, sizeof(refPart), "%s LAP %d",
                     reference.name.c_str(), reference.ref.lap);

            // Квадратик цвета кривой перед каждым именем: связь «имя ↔ линия на
            // графике» должна читаться взглядом, а не выводиться по памяти.
            const char* vs   = "   vs   ";
            const float chip = lSz * 0.62f;
            const float chipGap = chip * 0.55f;
            const float cpw  = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, cmpPart).x;
            const float vsw  = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, vs).x;
            const float rpw  = lf->CalcTextSizeA(lSz, FLT_MAX, 0.f, refPart).x;
            const float centreW = chip + chipGap + cpw + vsw + chip + chipGap + rpw;

            float x = base.x + (avail.x - centreW) * 0.5f;
            const float chipY = ty + (lSz - chip) * 0.5f;

            dl->AddRectFilled({x, chipY}, {x + chip, chipY + chip}, COL_GOLD, 2.f);
            x += chip + chipGap;
            dl->AddText(lf, lSz, {x, ty}, COL_GOLD, cmpPart);   x += cpw;
            dl->AddText(lf, lSz, {x, ty}, COL_LABEL, vs);       x += vsw;

            dl->AddRectFilled({x, chipY}, {x + chip, chipY + chip}, COL_REF, 2.f);
            x += chip + chipGap;
            dl->AddText(lf, lSz, {x, ty}, COL_REF, refPart);

            // Концы оси остаются на месте: это шкала, и убирать её нельзя.
            if (lw + centreW + rw + pad * 4.f < plotW) {
                dl->AddText(lf, lSz, {plotMin.x, ty}, COL_LABEL, left);
                dl->AddText(lf, lSz, {plotMax.x - rw, ty}, COL_LABEL, right);
            }
        } else {
            // Узкой панели концов оси хватает: где мы в круге, видно по ползунку.
            if (lw + mw + rw + pad * 4.f < plotW) {
                dl->AddText(lf, lSz, {plotMin.x, ty}, COL_LABEL, left);
                dl->AddText(lf, lSz, {plotMax.x - rw, ty}, COL_LABEL, right);
            }
            dl->AddText(lf, lSz, {base.x + (avail.x - mw) * 0.5f, ty}, COL_HDR_TEXT, middle);
        }
    }

    // ── Полоса перемотки ─────────────────────────────────────────────────────
    // Только на повторе: в живом заезде переставлять время некуда.
    if (replay.active) {
        const float cy = plotMax.y + axisH + scrubH * 0.5f;
        const float th = fmaxf(scrubH * 0.22f, 2.f);

        dl->AddRectFilled({plotMin.x, cy - th}, {plotMax.x, cy + th},
                          IM_COL32(40, 40, 40, 255), th);
        dl->AddRectFilled({plotMin.x, cy - th}, {stripHeadX, cy + th}, COL_GOLD_DIM, th);

        // Какой кусок круга сейчас под лупой. Без этой рамки приближенный
        // график теряет связь с кругом: непонятно, какую его часть смотришь.
        if (s_view_span < 1.f) {
            dl->AddRect({strip_x_of(s_view_min), cy - th * 2.f},
                        {strip_x_of(s_view_min + s_view_span), cy + th * 2.f},
                        IM_COL32(150, 150, 150, 140), th, 0, 1.f);
        }

        const float r = fmaxf(scrubH * 0.32f, 4.f);
        dl->AddCircleFilled({stripHeadX, cy}, r, COL_GOLD);
        dl->AddCircle({stripHeadX, cy}, r, IM_COL32(255, 235, 190, 200), 16, 1.f);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

}  // namespace Pro
