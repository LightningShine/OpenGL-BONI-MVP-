#include "ui/pro/ProTrackReport.h"
#include "core/Config.h"
#include "network/ReplayPlayer.h"
#include "core/WorldSnapshot.h"
#include "rendering/Interpolation.h"
#include "vehicle/Vehicle.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

extern std::vector<SplinePoint> g_smooth_track_points;
extern std::map<int32_t, Vehicle> g_vehicles;

namespace Pro {
namespace {

// ── Палитра полотна: как в Lite ─────────────────────────────────────────────
constexpr ImU32 TR_BORDER  = IM_COL32(200, 200, 200, 255);  // борта
constexpr ImU32 TR_ASPHALT = IM_COL32( 32,  32,  34, 255);  // асфальт
constexpr ImU32 TR_LINE    = IM_COL32( 70,  70,  74, 255);  // осевая (пунктир не нужен)

// Ширина полотна берётся ОТТУДА ЖЕ, откуда её берёт Lite (TrackConstants):
// панели доступна только осевая линия — сам меш полотна живёт на GPU, — поэтому
// полосу рисуем толстой линией по осевой. Своё число здесь означало бы, что
// трасса в этой панели шире или уже, чем на главном экране.
constexpr float TRACK_HALF_WIDTH  = TrackConstants::TRACK_ASPHALT_WIDTH * 0.5f;
constexpr float BORDER_HALF_WIDTH = TrackConstants::TRACK_BORDER_WIDTH  * 0.5f;

// Во сколько раз режим слежения приближает карту относительно вида целиком.
constexpr float FOLLOW_ZOOM = 5.0f;

// Насколько быстро камера догоняет машину (доля расхождения за секунду).
// Мгновенная привязка дёргает картинку на каждом пакете, слишком мягкая —
// машина уезжает от центра.
constexpr float CAMERA_CATCHUP = 8.0f;

enum class ColorMode { Speed, Flat, GForce };

struct ModeItem { ColorMode mode; const char* label; };
constexpr ModeItem MODES[] = {
    { ColorMode::Speed,  "SPEED"   },
    { ColorMode::GForce, "G-FORCE" },
    { ColorMode::Flat,   "PLAIN"   },
};

const char* modeLabel(ColorMode m)
{
    for (const ModeItem& item : MODES)
        if (item.mode == m) return item.label;
    return MODES[0].label;
}

// Наибольший разрыв между соседними точками траектории, ещё считающийся ездой.
//
// История пишется по ходу заезда, а перемотка повтора ВПЕРЁД проносит запись
// мимо: между двумя соседними точками оказывается весь промотанный участок.
// Соединять их линией нельзя — она срежет через поле, изображая проезд, которого
// не было. Такой разрыв честнее оставить разрывом.
// 0.15 нормализованных единиц = 15 м; при 10 Гц и 70 км/ч шаг около 2 м.
constexpr float MAX_TRAIL_STEP = 0.15f;

/// Цвет по доле 0..1: синий (медленно) → зелёный → жёлтый → красный (быстро).
/// Одна шкала на скорость и на перегрузку: обе читаются как «холодно/горячо».
ImU32 heatColor(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    struct Stop { float t; int r, g, b; };
    static const Stop stops[] = {
        { 0.00f,  40, 110, 220 },
        { 0.35f,  40, 190, 130 },
        { 0.65f, 225, 200,  60 },
        { 1.00f, 225,  60,  55 },
    };
    for (int i = 1; i < IM_ARRAYSIZE(stops); ++i) {
        if (t > stops[i].t) continue;
        const Stop& a = stops[i - 1];
        const Stop& b = stops[i];
        const float k = (b.t - a.t > 1e-6f) ? (t - a.t) / (b.t - a.t) : 0.f;
        return IM_COL32((int)(a.r + (b.r - a.r) * k),
                        (int)(a.g + (b.g - a.g) * k),
                        (int)(a.b + (b.b - a.b) * k), 255);
    }
    return IM_COL32(225, 60, 55, 255);
}

/// Рисует замкнутую полосу постоянной ширины отрезок за отрезком, закругляя
/// стыки кружком в каждой вершине.
///
/// ImGui соединяет звенья толстой полилинии «на ус» (miter), и на острых
/// изломах — а трасса из сплайна их полна — стык вылетает длинным шипом наружу.
/// Отдельные отрезки такого стыка не имеют вовсе, а кружок в вершине закрывает
/// клин между ними: получается ровно то, что рисует Lite.
void strokeClosedPath(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, float th)
{
    if (pts.size() < 2) return;
    const float r = th * 0.5f;
    for (size_t i = 0; i < pts.size(); ++i) {
        const ImVec2& a = pts[i];
        const ImVec2& b = pts[(i + 1) % pts.size()];
        dl->AddLine(a, b, col, th);
        dl->AddCircleFilled(a, r, col, 0);   // 0 = число сторон подберёт ImGui
    }
}

/// Точка траектории — копия того немногого, что нужно для отрисовки. Снимается
/// под мьютексом машин и дальше живёт сама: держать лок на всю отрисовку нельзя,
/// её и так ждёт приём телеметрии.
struct TrailPoint
{
    glm::vec2 pos{ 0.f, 0.f };
    float     speed = 0.f;
    float     g = 0.f;
};

}  // namespace

void RenderTrackReportWindow(const ProContext& ctx, int32_t vehicleId,
                             ImVec2 vpSz, float topH)
{
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({260.f * ui, topH + 60.f * ui},  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({620.f * ui, 520.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({240.f * ui, 200.f * ui}, {vpSz.x, vpSz.y});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG));
    if (!ImGui::Begin("##TrackReport", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    const float w = ImGui::GetWindowWidth();
    const float h = ImGui::GetWindowHeight();
    const float z = PanelZoom("TrackReport");
    DrawPanelHeader(ctx, "TRACK REPORT", false, "TrackReport");

    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const ImVec2 wp   = ImGui::GetWindowPos();

    // ── Переключатели в шапке ────────────────────────────────────────────────
    // Слева направо: режим окраски и слежение. Рисуются прямо в список
    // отображения по вычисленным точкам — как ROTATE в TRACK MAP.
    static ColorMode s_mode   = ColorMode::Speed;
    static bool      s_follow = false;

    const float hgap = ui_scale::points(28.f);

    // Кнопка слежения — у правого края, левее крестика закрытия.
    bool followClicked = false;
    const float followX = HeaderButton(ctx, "FOLLOW", s_follow, false, wp.x + w - hgap,
                                       followClicked);
    if (followClicked) s_follow = !s_follow;

    // Кнопка режима окраски — левее слежения. Подпись и есть текущий режим, а
    // клик открывает список: перебор по кругу заставлял щёлкать вслепую, пока не
    // попадётся нужный, и не показывал, из чего вообще выбирают.
    bool modeClicked = false;
    const float modeX = HeaderButton(ctx, modeLabel(s_mode), false, /*arrow=*/true,
                                     followX - ui_scale::points(14.f), modeClicked);
    if (modeClicked) ImGui::OpenPopup("##trackReportMode");

    ImGui::SetNextWindowPos({ modeX - ui_scale::points(8.f), wp.y + header_h() });
    PushDropdownStyle();
    if (ImGui::BeginPopup("##trackReportMode")) {
        // Шрифт тот же, что у пунктов навбара.
        if (ctx.regular) ImGui::PushFont(ctx.regular);
        for (const ModeItem& item : MODES)
            if (ImGui::MenuItem(item.label, nullptr, item.mode == s_mode))
                s_mode = item.mode;
        if (ctx.regular) ImGui::PopFont();
        ImGui::EndPopup();
    }
    PopDropdownStyle();

    const float mapH = h - header_h() - 2.f;
    const ImVec2 mapMin = base;
    const ImVec2 mapMax = { base.x + w, base.y + mapH };
    dl->AddRectFilled(mapMin, mapMax, COL_BG);

    if (g_smooth_track_points.empty()) {
        const char* msg = "No track loaded";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText({ base.x + (w - ts.x) * 0.5f, base.y + (mapH - ts.y) * 0.5f },
                    COL_DIM, msg);
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    // ── Данные машины: позиция и траектория текущего круга ───────────────────
    // Позиция — из снимка (тот же момент, что у всех панелей), траектория — из
    // истории сэмплов под мьютексом машин: в снимке её нет.
    glm::vec2 carPos{ 0.f, 0.f };
    float     carHeading = 0.f;
    bool      haveCar = false;
    glm::vec2 renderOffset{ 0.f, 0.f };
    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        if (const world::VehicleView* v = world::find(*snapshot, vehicleId)) {
            renderOffset = v->apply_track_render_offset ? getTrackRenderOffset()
                                                        : glm::vec2(0.f, 0.f);
            carPos = { (float)v->x + renderOffset.x, (float)v->y + renderOffset.y };
            carHeading = (float)v->heading;
            haveCar = true;
        }
    }

    std::vector<TrailPoint> trail;
    float trailSpeedMin = FLT_MAX, trailSpeedMax = 0.f, trailGMax = 0.f;
    {
        // Линия проезда — КРУГ ЦЕЛИКОМ, а не по точку просмотра. На повторе
        // разбирают уже случившийся заезд, и обрезанная линия отвечает на
        // вопрос «где машина побывала» ровно наполовину; только что начавшийся
        // круг выглядел бы пустым, хотя в записи он есть весь.
        //
        // Поэтому на повторе источник — журнал записи (построен при открытии,
        // перемоткой не задевается), а в живом заезде — история самой машины:
        // там впереди машины данных не существует.
        auto collect = [&](const std::vector<LapInfo>& samples) {
            trail.reserve(samples.size());
            for (const LapInfo& s : samples) {
                if (s.x == 0.0 && s.y == 0.0) continue;   // сэмпл до появления координат
                TrailPoint p;
                p.pos   = { (float)s.x, (float)s.y };
                p.speed = s.speed;
                p.g     = sqrtf(s.gForceX * s.gForceX + s.gForceY * s.gForceY);
                trail.push_back(p);
            }
        };

        // Линию рисуем для того круга, который РАЗБИРАЮТ, а не для того, где
        // стоит запись. Круг выбирает оператор щелчком в LAP LIST, и GRAPHS с
        // самим списком уже идут за этим выбором. Пока панель шла за точкой
        // просмотра, выбор круга с последующим движением ползунка внутри
        // другого показывал на экране два разных круга одновременно.
        //
        // AnalysisLap спрашиваем ДО мьютекса машин: внутри он ходит в
        // RaceManager, а тот берёт тот же мьютекс.
        const int analysisLap = AnalysisLap(vehicleId);

        VehiclesLock lk;
        const auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            const Vehicle& v = it->second;
            const std::shared_ptr<const telemetry::VehicleJournal> journal =
                telemetry::replay_journal(vehicleId);

            if (journal != nullptr) {
                const auto lap = journal->lap_samples.find(analysisLap);
                if (lap != journal->lap_samples.end()) collect(lap->second);
            } else {
                const auto lap = v.laps.find(analysisLap);
                if (lap != v.laps.end()) collect(lap->second.samples);
            }
        }
    }
    // ── Линия проезда круга-образца ──────────────────────────────────────────
    // Белая, тоньше и без раскраски по скорости: цветом здесь говорит разбираемый
    // круг, а образцу достаточно показать САМУ ТРАЕКТОРИЮ — где он ехал иначе.
    std::vector<glm::vec2> refTrail;
    if (ComparisonActive()) {
        const RefTrace& reference = Reference();
        refTrail.reserve(reference.samples.size());
        for (const LapInfo& sample : reference.samples) {
            if (sample.x == 0.0 && sample.y == 0.0) continue;
            refTrail.push_back({ static_cast<float>(sample.x), static_cast<float>(sample.y) });
        }
    }

    for (const TrailPoint& p : trail) {
        trailSpeedMin = fminf(trailSpeedMin, p.speed);
        trailSpeedMax = fmaxf(trailSpeedMax, p.speed);
        trailGMax     = fmaxf(trailGMax, p.g);
    }
    if (trail.empty()) trailSpeedMin = 0.f;

    // ── Камера ───────────────────────────────────────────────────────────────
    // Север вверху при любом раскладе: карта не вращается, поворачивается только
    // значок машины. Масштаб «весь трек» считается всегда — от него же берётся
    // приближение в режиме слежения, поэтому оно не зависит от размера окна.
    glm::vec2 lo = g_smooth_track_points[0].position;
    glm::vec2 hi = lo;
    for (const SplinePoint& sp : g_smooth_track_points) {
        lo.x = fminf(lo.x, sp.position.x); lo.y = fminf(lo.y, sp.position.y);
        hi.x = fmaxf(hi.x, sp.position.x); hi.y = fmaxf(hi.y, sp.position.y);
    }
    const float rX = fmaxf(hi.x - lo.x + TRACK_HALF_WIDTH * 2.f, 1e-6f);
    const float rY = fmaxf(hi.y - lo.y + TRACK_HALF_WIDTH * 2.f, 1e-6f);
    const float pad = ui_scale::points(14.f);
    const float fitScale = fminf((w - pad * 2.f) / rX, (mapH - pad * 2.f) / rY);

    const glm::vec2 trackCentre{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f };

    static glm::vec2 s_camera = trackCentre;
    static bool      s_cameraInit = false;
    if (!s_cameraInit) { s_camera = trackCentre; s_cameraInit = true; }

    const bool following = s_follow && haveCar;
    const glm::vec2 target = following ? carPos : trackCentre;
    if (following) {
        // Догоняем машину плавно, кадрово-независимо.
        const float dt = ImGui::GetIO().DeltaTime;
        const float k = 1.f - expf(-CAMERA_CATCHUP * fmaxf(dt, 1e-4f));
        s_camera += (target - s_camera) * k;
    } else {
        s_camera = trackCentre;
    }
    const float scale = following ? fitScale * FOLLOW_ZOOM : fitScale;

    const ImVec2 mapCentre{ (mapMin.x + mapMax.x) * 0.5f, (mapMin.y + mapMax.y) * 0.5f };
    auto toScreen = [&](glm::vec2 p) -> ImVec2 {
        return { mapCentre.x + (p.x - s_camera.x) * scale,
                 mapCentre.y - (p.y - s_camera.y) * scale };   // экранный Y вниз
    };

    // Всё, что за пределами панели, обрезаем: в режиме слежения трасса заведомо
    // шире окна, и без этого она рисовалась бы поверх соседних панелей.
    dl->PushClipRect(mapMin, mapMax, true);

    // ── Полотно трассы: борта, потом асфальт поверх ──────────────────────────
    {
        const size_t n = g_smooth_track_points.size();
        std::vector<ImVec2> pts;
        pts.reserve(n);
        for (const SplinePoint& sp : g_smooth_track_points)
            pts.push_back(toScreen(sp.position));

        const float asphaltPx = fmaxf(TRACK_HALF_WIDTH  * 2.f * scale, 3.f);
        const float borderPx  = fmaxf(BORDER_HALF_WIDTH * 2.f * scale, asphaltPx + 2.f);

        strokeClosedPath(dl, pts, TR_BORDER,  borderPx);
        strokeClosedPath(dl, pts, TR_ASPHALT, asphaltPx);

        // Линия старт/финиша — поперёк полотна в первой точке трассы.
        if (n > 1) {
            const glm::vec2 dir = glm::normalize(g_smooth_track_points[1].position -
                                                 g_smooth_track_points[0].position);
            const glm::vec2 nrm{ -dir.y, dir.x };
            const glm::vec2 a = g_smooth_track_points[0].position + nrm * TRACK_HALF_WIDTH;
            const glm::vec2 b = g_smooth_track_points[0].position - nrm * TRACK_HALF_WIDTH;
            dl->AddLine(toScreen(a), toScreen(b), COL_WHITE, fmaxf(asphaltPx * 0.12f, 1.5f));
        }
    }

    // ── Траектория ───────────────────────────────────────────────────────────
    if (trail.size() >= 2) {
        // Вдвое тоньше полотна-четверти: толстая линия закрывала сам асфальт, и
        // по ней не было видно, ближе к какому краю трассы шла машина.
        const float th = fmaxf(TRACK_HALF_WIDTH * 0.25f * scale, 1.5f);
        const float spanSpeed = fmaxf(trailSpeedMax - trailSpeedMin, 1e-3f);
        const float spanG     = fmaxf(trailGMax, 0.3f);

        for (size_t i = 1; i < trail.size(); ++i) {
            const TrailPoint& a = trail[i - 1];
            const TrailPoint& b = trail[i];

            // Перемотка вперёд пронесла запись мимо: соединять эти две точки —
            // значит нарисовать проезд напрямик через поле (см. MAX_TRAIL_STEP).
            const glm::vec2 step = b.pos - a.pos;
            if (step.x * step.x + step.y * step.y > MAX_TRAIL_STEP * MAX_TRAIL_STEP)
                continue;

            ImU32 col;
            switch (s_mode) {
                case ColorMode::Speed:
                    col = heatColor((b.speed - trailSpeedMin) / spanSpeed);
                    break;
                case ColorMode::GForce:
                    col = heatColor(b.g / spanG);
                    break;
                default:
                    col = COL_GOLD;
                    break;
            }
            const ImVec2 pa = toScreen(a.pos + renderOffset);
            const ImVec2 pb = toScreen(b.pos + renderOffset);
            dl->AddLine(pa, pb, col, th);
            // Стык закругляем, иначе на каждом изломе линия рвётся клином
            // наружу — на приближении это видно как зубцы вдоль всей траектории.
            dl->AddCircleFilled(pb, th * 0.5f, col, 0);
        }
    }

    // ── Траектория образца: ПОВЕРХ своей ─────────────────────────────────────
    //
    // Тонкой сплошной белой нитью. Под ней остаётся видна цветная полоса
    // разбираемого круга — она шире, — а сама нить показывает, где образец
    // выбирал другую траекторию. Рисовать её первой было бесполезно: цветная
    // полоса накрывала её целиком, и панель выглядела нетронутой сравнением.
    if (refTrail.size() >= 2) {
        const float rth = fmaxf(TRACK_HALF_WIDTH * 0.10f * scale, 1.5f);
        for (size_t i = 1; i < refTrail.size(); ++i) {
            const glm::vec2 step = refTrail[i] - refTrail[i - 1];
            if (step.x * step.x + step.y * step.y > MAX_TRAIL_STEP * MAX_TRAIL_STEP)
                continue;
            dl->AddLine(toScreen(refTrail[i - 1] + renderOffset),
                        toScreen(refTrail[i]     + renderOffset), COL_REF, rth);
        }
    }

    // ── Машина-призрак: образец в ТУ ЖЕ СЕКУНДУ круга ────────────────────────
    //
    // Вторая машина на карте. Стоит она не там же, где наша: обе прошли по
    // трассе одинаковое ВРЕМЯ круга, а расстояние между ними — это и есть
    // накопленный разрыв, видимый без единой цифры. Сравнивать положения по
    // одному и тому же МЕСТУ трассы было бы бессмысленно: место одно, значки
    // легли бы друг на друга.
    {
        LapInfo ghost;
        if (haveCar && ReferenceGhost(vehicleId, ghost)) {
            const ImVec2 g = toScreen({ static_cast<float>(ghost.x) + renderOffset.x,
                                        static_cast<float>(ghost.y) + renderOffset.y });
            const float  gr = fmaxf(TRACK_HALF_WIDTH * 0.9f * scale, 5.f);
            dl->AddCircleFilled(g, gr * 0.55f, COL_REF_DIM);
            dl->AddCircle(g, gr, COL_REF, 24, 1.5f);
        }
    }

    // ── Машина ───────────────────────────────────────────────────────────────
    if (haveCar) {
        const ImVec2 c = toScreen(carPos);
        const float  r = fmaxf(TRACK_HALF_WIDTH * 0.9f * scale, 5.f);
        // Треугольник по курсу: карта не вращается, поэтому направление движения
        // показывает сам значок.
        const float ch = cosf(carHeading), sh = sinf(carHeading);
        auto rot = [&](float dx, float dy) -> ImVec2 {
            return { c.x + dx * ch - dy * sh, c.y - (dx * sh + dy * ch) };
        };
        dl->AddTriangleFilled(rot(r, 0.f), rot(-r * 0.7f, r * 0.7f), rot(-r * 0.7f, -r * 0.7f),
                              COL_WHITE);
        dl->AddCircle(c, r * 1.6f, IM_COL32(255, 255, 255, 70), 24, 1.f);
    }

    dl->PopClipRect();

    // ── Подпись состояния ────────────────────────────────────────────────────
    {
        char info[160];
        if (trail.size() >= 2 && s_mode == ColorMode::Speed)
            snprintf(info, sizeof(info), "%.0f - %.0f km/h", trailSpeedMin, trailSpeedMax);
        else if (trail.size() >= 2 && s_mode == ColorMode::GForce)
            snprintf(info, sizeof(info), "0 - %.2f g", trailGMax);
        else if (trail.empty())
            snprintf(info, sizeof(info), "waiting for the lap to start");
        else
            snprintf(info, sizeof(info), "current lap");

        // Чей второй след — здесь же: белая линия без имени не отвечает на
        // первый вопрос, который к ней возникает.
        if (ComparisonActive()) {
            const RefTrace& reference = Reference();
            char line[160];
            snprintf(line, sizeof(line), "%s   vs %s L%d",
                     info, reference.name.c_str(), reference.ref.lap);
            snprintf(info, sizeof(info), "%s", line);
        }

        ImFont* lf = ctx.russo ? ctx.russo : ImGui::GetFont();
        const float lsz = lf->FontSize * z;
        dl->AddText(lf, lsz, { mapMin.x + pad, mapMax.y - lsz - pad * 0.5f }, COL_LABEL, info);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

}  // namespace Pro
