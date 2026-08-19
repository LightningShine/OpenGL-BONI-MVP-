#include "ProTrackReport.h"
#include "../../Config.h"
#include "../../core/WorldSnapshot.h"
#include "../../rendering/Interpolation.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

extern std::vector<SplinePoint> g_smooth_track_points;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

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

    ImFont*     hf   = ctx.russo ? ctx.russo : ImGui::GetFont();
    const float hsz  = ui_scale::points(UIConfig::FONT_PT_RUSSO_SMALL);
    const float hgap = ui_scale::points(28.f);

    // Рисует надпись-кнопку правым краем в `rightEdge`, возвращает её левый край
    // и сообщает о клике. Одна точка на отрисовку и на попадание мышью: считать
    // прямоугольник дважды — верный способ развести их при первой же правке.
    //
    // Надписи серые, как и подпись шапки: это служебные переключатели, а не
    // данные. Включённое состояние показывает яркость, а не другой цвет —
    // золотом на этом экране выделены значения, и спорить с ними шапка не должна.
    // `arrow` дорисовывает треугольник: у надписи, за которой открывается
    // список, должен быть признак, что она раскрывается.
    auto headerButton = [&](const char* label, bool active, bool arrow, float rightEdge,
                            bool& clicked) -> float {
        const float tw   = hf->CalcTextSizeA(hsz, FLT_MAX, 0.f, label).x;
        const float ah   = arrow ? ui_scale::points(4.f) : 0.f;
        const float gap  = arrow ? ui_scale::points(5.f) : 0.f;
        const float full = tw + gap + (arrow ? ah * 2.f : 0.f);
        const float x    = rightEdge - full;
        const float grow = ui_scale::points(4.f);
        const bool hov = ImGui::IsMouseHoveringRect({ x - grow, wp.y },
                                                    { x + full + grow, wp.y + header_h() }, false);

        const ImU32 col = (active || hov) ? COL_HDR_TEXT : COL_LABEL;
        const float ty  = wp.y + (header_h() - hsz) * 0.5f;
        dl->AddText(hf, hsz, { x, ty }, col, label);

        if (arrow) {
            const float ax = x + tw + gap;
            const float ay = wp.y + header_h() * 0.5f;
            dl->AddTriangleFilled({ ax, ay - ah * 0.5f }, { ax + ah * 2.f, ay - ah * 0.5f },
                                  { ax + ah, ay + ah * 0.9f }, col);
        }

        clicked = hov && ImGui::IsMouseClicked(0);
        return x;
    };

    // Кнопка слежения — у правого края, левее крестика закрытия.
    bool followClicked = false;
    const float followX = headerButton("FOLLOW", s_follow, false, wp.x + w - hgap, followClicked);
    if (followClicked) s_follow = !s_follow;

    // Кнопка режима окраски — левее слежения. Подпись и есть текущий режим, а
    // клик открывает список: перебор по кругу заставлял щёлкать вслепую, пока не
    // попадётся нужный, и не показывал, из чего вообще выбирают.
    bool modeClicked = false;
    const float modeX = headerButton(modeLabel(s_mode), false, /*arrow=*/true,
                                     followX - ui_scale::points(14.f), modeClicked);
    if (modeClicked) ImGui::OpenPopup("##trackReportMode");

    // Список оформлен ТЕМИ ЖЕ константами, что и выпадающие меню навбара
    // (UIConfig::DROPDOWN_*). Свой набор цветов здесь означал бы, что в одном
    // приложении два разных выпадающих меню, и разойтись им достаточно одной
    // правки палитры.
    const ImVec2 dsz = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({ modeX - ui_scale::points(8.f), wp.y + header_h() });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        { UIConfig::DROPDOWN_PADDING_X * dsz.x,
                          UIConfig::DROPDOWN_PADDING_Y * dsz.y });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UIConfig::DROPDOWN_BORDER_SIZE);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   UIConfig::DROPDOWN_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        { UIConfig::DROPDOWN_ITEM_SPACING_X * dsz.x,
                          UIConfig::DROPDOWN_ITEM_SPACING_Y * dsz.y });
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        { UIConfig::DROPDOWN_ITEM_PADDING_X * dsz.x,
                          UIConfig::DROPDOWN_ITEM_PADDING_Y * dsz.y });
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
    if (ImGui::BeginPopup("##trackReportMode")) {
        // Шрифт тот же, что у пунктов навбара.
        if (ctx.regular) ImGui::PushFont(ctx.regular);
        for (const ModeItem& item : MODES)
            if (ImGui::MenuItem(item.label, nullptr, item.mode == s_mode))
                s_mode = item.mode;
        if (ctx.regular) ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(5);

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
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        const auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            const Vehicle& v = it->second;
            const auto lap = v.laps.find(v.m_current_lap_number);
            if (lap != v.laps.end()) {
                const auto& smp = lap->second.samples;
                // Ровно та часть круга, что УЖЕ проехана на текущей точке: на
                // повторе в истории лежит и то, что случится дальше.
                const size_t n = visibleSampleCount(v, v.m_current_lap_number, smp);
                trail.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    const LapInfo& s = smp[i];
                    if (s.x == 0.0 && s.y == 0.0) continue;   // сэмпл до этой правки
                    TrailPoint p;
                    p.pos   = { (float)s.x, (float)s.y };
                    p.speed = s.speed;
                    p.g     = sqrtf(s.gForceX * s.gForceX + s.gForceY * s.gForceY);
                    trail.push_back(p);
                }
            }
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
        char info[96];
        if (trail.size() >= 2 && s_mode == ColorMode::Speed)
            snprintf(info, sizeof(info), "%.0f - %.0f km/h", trailSpeedMin, trailSpeedMax);
        else if (trail.size() >= 2 && s_mode == ColorMode::GForce)
            snprintf(info, sizeof(info), "0 - %.2f g", trailGMax);
        else if (trail.empty())
            snprintf(info, sizeof(info), "waiting for the lap to start");
        else
            snprintf(info, sizeof(info), "current lap");

        ImFont* lf = ctx.russo ? ctx.russo : ImGui::GetFont();
        const float lsz = lf->FontSize * z;
        dl->AddText(lf, lsz, { mapMin.x + pad, mapMax.y - lsz - pad * 0.5f }, COL_LABEL, info);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

}  // namespace Pro
