#pragma once
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "../../vehicle/LapTypes.h"   // LapInfo — образец круга живёт здесь
#include "../ui_scale.hpp"
#include "../UI_Config.h"   // FONT_PT_RUSSO_SMALL — кегль подписи в шапке

struct ProContext {
    ImFont* regular;   // Ubuntu Regular ~12px (menu size)
    ImFont* bold;      // Ubuntu Bold ~16px
    ImFont* title;     // Russo One ~32px (large lap time)
    ImFont* mono;      // Roboto Mono
    ImFont* russo;     // Russo One small ~13px (panel labels/numbers)
    ImFont* jb;        // JetBrains Mono Bold ~32px (sector labels/times)
    void*   logoTex;
    void*   numTex[9]; // styles/icons/PNG/1..9 PNG.png — иконки групп бокового меню
};

namespace Pro {

// ── Time formatters ─────────────────────────────────────────────────────────
inline void fmtTime(float s, char* buf, size_t n) {
    if (s <= 0.f) { snprintf(buf, n, "--:--.---"); return; }
    int m = (int)(s / 60.f);
    float r = s - m * 60.f;
    snprintf(buf, n, "%d:%06.3f", m, r);
}
inline void fmtDelta(float d, char* buf, size_t n) {
    if (d == 0.f) { snprintf(buf, n, "---"); return; }
    snprintf(buf, n, "%+.3f", d);
}

// ── Color palette ────────────────────────────────────────────────────────────
static constexpr ImU32 COL_GOLD       = IM_COL32(218, 165,  64, 255);
static constexpr ImU32 COL_GOLD_DIM   = IM_COL32(218, 165,  64,  60);
static constexpr ImU32 COL_GREEN      = IM_COL32(  0, 210, 110, 255);
static constexpr ImU32 COL_CYAN       = IM_COL32(  0, 188, 255, 255);
static constexpr ImU32 COL_RED        = IM_COL32(255,  75,  75, 255);
static constexpr ImU32 COL_WHITE      = IM_COL32(255, 255, 255, 255);
static constexpr ImU32 COL_TEXT       = IM_COL32(220, 220, 220, 255);
static constexpr ImU32 COL_DIM        = IM_COL32(110, 110, 110, 255);
static constexpr ImU32 COL_LABEL      = IM_COL32(0x51, 0x51, 0x51, 255); // #515151
static constexpr ImU32 COL_HDR_BG     = IM_COL32( 28,  28,  28, 255);
static constexpr ImU32 COL_HDR_TEXT   = IM_COL32(210, 210, 210, 255); // подпись шапки — одна на все панели
static constexpr ImU32 COL_SEP        = IM_COL32( 48,  48,  48, 255);
static constexpr ImU32 COL_BG         = IM_COL32( 13,  13,  13, 255);
static constexpr ImU32 COL_BG_PANEL   = IM_COL32( 20,  20,  20, 255);
static constexpr ImU32 COL_BG_WIDGET  = IM_COL32( 16,  16,  16, 255);
// Sector accent colors
static constexpr ImU32 COL_S1         = IM_COL32( 85, 130, 225, 255);
static constexpr ImU32 COL_S2         = IM_COL32(165,  85, 210, 255);
static constexpr ImU32 COL_S3         = IM_COL32(220,  70,  70, 255);

// ── Layout constants ─────────────────────────────────────────────────────────
// Пункты × DPI (ui_scale): панели держат физический размер на любом мониторе
inline float pad_px()    { return ui_scale::points(10.f); } // horizontal content padding
inline float header_h()  { return ui_scale::points(26.f); } // panel header height

// Layout lock — toggled from View menu; persists per session
extern bool g_pro_layout_locked;

// Заморозка раскладки на переходные кадры смены DPI-масштаба: пока окно ОС
// догоняет новый размер, клэмп панелей и перенос позиций НЕ работают —
// иначе они «чинят» раскладку по рассинхронённому состоянию и панели
// навсегда уезжают (порча за 2 переезда между мониторами).
// Выставляет UI::apply_ui_scale_change, декрементирует UI::BeginFrame.
extern int g_layout_freeze_frames;

// Per-panel text zoom. Call once just after a panel's Begin() — handles
// Ctrl+wheel / Ctrl +/- on the focused/hovered window, persists the level to
// disk, and returns the scale factor the panel should multiply its fonts and
// paddings by. `key` must be a stable per-panel id (e.g. "LapList").
float PanelZoom(const char* key);

// ── Отложенная запись настроек панелей ──────────────────────────────────────
// Зум панели и её видимость лежат в .ini рядом с программой. Раньше каждое
// изменение открывало, переписывало и закрывало файл прямо в обработчике ввода:
// десять щелчков колеса — десять перезаписей файла на диск, и всё это внутри
// кадра отрисовки. Теперь изменение только помечает файл грязным, а запись
// уходит на паузу в вводе (или на выход из программы).
//
// `force` пишет немедленно — так закрывается приложение.
void FlushPanelSettings(bool force = false);

// ── Часы заезда для панелей ─────────────────────────────────────────────────
// Секунды текущей точки ЗАЕЗДА, а не времени работы приложения.
//
// На повторе это позиция в записи. Разница принципиальна для всего, что живёт
// во времени: удержание секторов на карте, отметки в журнале событий. Пока они
// считали по стенным часам, картинка менялась сама по себе на стоящем повторе —
// оператор смотрит в одну точку записи, а панель через десять секунд показывает
// другое. По этим же часам события отбрасываются при перемотке назад.
float SessionTimeSeconds();

// ── Круг, который разбирают панели ──────────────────────────────────────────
// Список кругов — это ОГЛАВЛЕНИЕ записи, и выбранный в нём круг обязан
// показываться панелями сам по себе, а не через то, куда попадёт перемотка.
// Пока панели шли за m_current_lap_number машины, выбор круга работал только
// если гоночная логика успевала сойтись к нужному кругу: щелчок по второму
// кругу показывал третий, а подсветка стояла сразу на двух строках — на
// выбранной и на той, где стоит запись.
//
// Выбор держится, пока запись СТОИТ (перемотка по выбору круга сама ставит её
// на паузу). Нажали «играть» — панели снова идут за повтором: это и есть
// обычный просмотр.
//
// AnalysisLap возвращает круг повтора, если выбора нет.
int  AnalysisLap(int32_t vehicleId);
void SelectAnalysisLap(int lapNumber);   // -1 — снять выбор

// ── СРАВНЕНИЕ ДВУХ КРУГОВ ───────────────────────────────────────────────────
//
// Круг сам по себе не говорит почти ничего: «46.190» — это быстро или медленно,
// и где именно потеряно время? Ответ даёт только второй круг рядом. Поэтому у
// разбора две стороны:
//
//   COMPARE — круг, который разбирают. Его данные идут в цветах панелей, как и
//             раньше; на графиках это привычные кривые.
//   REF     — круг-образец, С КОТОРЫМ сравнивают. Везде показывается БЕЛЫМ:
//             один цвет на всех панелях, чтобы взгляд не приходилось
//             перенастраивать, переходя с графика на шкалу перегрузки.
//
// Стороны выбираются двумя квадратиками в строке LAP LIST. Круг может быть и
// СВОИМ (соседний круг того же гонщика), и ЧУЖИМ: REF помнит не только номер
// круга, но и машину, поэтому достаточно переключиться на другого гонщика,
// отметить его круг как REF и вернуться к своему.
//
// Пока REF не выбран, всё работает ровно как раньше — ни одна панель не
// меняется.
struct LapRef
{
    int32_t vehicle = -1;
    int     lap     = 0;
    bool valid() const { return vehicle >= 0; }
};
inline bool operator==(const LapRef& a, const LapRef& b) {
    return a.vehicle == b.vehicle && a.lap == b.lap;
}
inline bool operator!=(const LapRef& a, const LapRef& b) { return !(a == b); }

LapRef ReferenceLap();
LapRef ComparePin();

/// Машина, которую РАЗБИРАЮТ. Это закреплённая сторона COMPARE, если она есть,
/// и выбранная в боковом меню машина, если нет.
///
/// Отличается от машины, чей СПИСОК КРУГОВ открыт: список — оглавление записи,
/// по нему ходят, чтобы найти круг другого гонщика и отметить его образцом.
/// Пока панели шли за списком, такой поход подменял разбираемую сторону.
int32_t AnalysisVehicle(int32_t displayVehicleId);

/// Отмечает круг стороной REF/COMPARE. Повторный вызов с тем же кругом СНИМАЕТ
/// отметку: квадратик — это переключатель, и снять его надо тем же движением,
/// которым поставили.
void PinReferenceLap(const LapRef& lap);
void PinCompareLap(const LapRef& lap);

/// Снимает обе стороны. Нужна как выход из сравнения, не зависящий от того,
/// чей список кругов сейчас открыт.
void ClearComparison();

/// Круг-образец целиком. Снимается РАЗ В КАДР (RefreshReference), а панели
/// читают уже готовое: иначе каждая из семи панелей копировала бы историю круга
/// заново на каждом кадре.
struct RefTrace
{
    bool                 valid = false;
    LapRef               ref;
    std::string          name;            // имя машины REF; пусто — нет имени
    float                lap_time = 0.f;  // время круга-образца, с; 0 — нет
    // Времена секторов круга — измеренные, те же, что в LapData.
    // SECTOR_TIME_NONE — сектор не измерен.
    std::array<float, SECTOR_COUNT> sectors{
        SECTOR_TIME_NONE, SECTOR_TIME_NONE, SECTOR_TIME_NONE };
    std::vector<LapInfo> samples;         // упорядочены по прогрессу
    size_t               source_size = 0; // сколько замеров было у источника

    /// Замер на доле круга `d` (0..1) линейной интерполяцией между соседними.
    /// false — образец этот участок круга не покрывает: показывать нечего, и
    /// врать средним по кругу нельзя.
    bool at(double d, LapInfo& out) const;

    /// Замер на `seconds` секунде круга. Нужен машине-призраку: «где образец
    /// был в ту же секунду круга» — это и есть разрыв между двумя проездами,
    /// видимый прямо на карте.
    bool at_time(float seconds, LapInfo& out) const;
};

/// Готовый образец. Пустой (valid == false), пока REF не выбран.
const RefTrace& Reference();

/// Замер образца в ТОЙ ЖЕ ТОЧКЕ ТРАССЫ, где машина `vehicleId` стоит сейчас.
///
/// Именно по месту на трассе, а не по времени: панели мгновенных значений
/// (скорость, перегрузки, каналы) отвечают на вопрос «а сколько здесь было у
/// образца», и сравнивать надо один и тот же поворот, а не одну и ту же секунду
/// круга — к середине круга секунды разъезжаются.
///
/// false — сравнение выключено, машины нет в снимке или образец этот участок
/// круга не покрывает.
bool ReferenceAtVehicle(int32_t vehicleId, LapInfo& out);

/// Где образец был в ТУ ЖЕ СЕКУНДУ круга, на которой сейчас машина `vehicleId`.
///
/// Это вторая машина на карте разбора — «призрак». По одному и тому же месту
/// трассы (ReferenceAtVehicle) сравнивают ЗНАЧЕНИЯ каналов; по одной и той же
/// секунде круга — ПОЛОЖЕНИЕ, и расстояние между машинами на карте прямо
/// показывает накопленный разрыв.
///
/// false — сравнение выключено, круг ещё не начат или образец так далеко не
/// доехал.
bool ReferenceGhost(int32_t vehicleId, LapInfo& out);

/// Разбираемый круг целиком — вторая сторона сравнения, снимается так же, как
/// образец. Пусто, если сравнение выключено или круга нет.
const RefTrace& Analysed();

/// ОТСТАВАНИЕ разбираемого круга от образца в точке, где машина сейчас.
///
/// Единственное определение на все панели: график, LAPTIME и всё, что покажет
/// эту величину дальше, обязаны брать её отсюда. Иначе получается то, ради чего
/// эта функция и заведена: два «правильных» числа рядом, отличающихся на шаг
/// замера, и разобрать по ним ничего нельзя.
///
/// Обе стороны интерполируются по своим замерам на ОДНОЙ доле круга. Живой
/// таймер круга сюда не годится: у образца такого таймера нет, и сравнивать
/// пришлось бы разные по природе величины.
///
/// false — сравнение выключено или один из кругов этот участок не покрывает.
bool ComparisonDelta(int32_t vehicleId, float& out);

/// Сравнение включено: REF выбран и его данные прочитаны.
bool ComparisonActive();

/// Пересобирает обе стороны сравнения, если они устарели. Зовёт Pro::Render раз
/// в кадр, ПОСЛЕ того как определена выбранная машина и ДО отрисовки панелей.
void RefreshReference(int32_t displayVehicleId);

/// Белый — цвет стороны REF на всех панелях разом.
inline constexpr ImU32 COL_REF = IM_COL32(0xEC, 0xEC, 0xEC, 255);
inline constexpr ImU32 COL_REF_DIM = IM_COL32(0xEC, 0xEC, 0xEC, 150);

// ── Смена открытой записи ───────────────────────────────────────────────────
// Панели копят состояние у себя: выбранный круг, выбранная машина, приближение
// графика, журнал событий. Всё это относится к КОНКРЕТНОЙ записи, а у модуля
// повтора долго не было точки, на которую можно подписаться, — и открытие
// второй записи оставляло приближение от первой, выбор машины на чужом номере
// и чужие события в журнале.
//
// Панель держит свой `seen` и зовёт это в начале отрисовки: true возвращается
// РОВНО ОДИН РАЗ на каждую смену записи (открытие или закрытие).
bool ReplaySessionChanged(uint64_t& seen);

// Flags for all floating panels — NoMove/NoResize added when layout is locked
//
// NoNav обязателен. В приложении включена клавиатурная навигация ImGui
// (ImGuiConfigFlags_NavEnableKeyboard), и как только окно с элементами получает
// фокус, io.WantCaptureKeyboard становится истинным — а по нему processInput в
// main.cpp отдаёт клавиатуру интерфейсу целиком. Достаточно щёлкнуть по панели,
// и транспорт повтора (стрелки, пробел) переставал отвечать. Панели тут ни при
// чём: они управляются мышью, ходить по ним стрелками не нужно и не задумано.
inline ImGuiWindowFlags PanelFlags() {
    return ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
           ImGuiWindowFlags_NoNav |
           (g_pro_layout_locked ? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) : 0);
}

// ── Panel visibility (боковое меню в стиле McLaren) ──────────────────────────
// Видимость каждой плавающей панели — переключатель, персистится в
// pro_panels.ini. Ключ совпадает с ключом PanelZoom (напр. "TrackMap").
// Реализация — в ProSidebar.cpp.
bool PanelVisible(const char* key);
void SetPanelVisible(const char* key, bool v);
void TogglePanel(const char* key);

// ── Panel header ─────────────────────────────────────────────────────────────
// Draws a dark header bar at the current cursor position using DrawList
// (does not affect ImGui cursor). Then advances cursor via Dummy.
// closeKey != nullptr рисует крестик закрытия справа в шапке; клик по нему
// прячет панель (SetPanelVisible(closeKey, false)) — так окно можно «убрать».
//
// ШАПКА ОДИНАКОВА У ВСЕХ ПАНЕЛЕЙ и не подчиняется ни зуму панели, ни выбору
// шрифта на месте вызова. Раньше подпись бралась шрифтом на усмотрение панели
// (LAP LIST — жирным Ubuntu, остальные — Russo) и умножалась на зум панели: у
// растянутой панели заголовок вырастал вдвое, у сжатой скукоживался, и колонка
// панелей выглядела собранной из четырёх разных программ. Зум панели — про
// СОДЕРЖИМОЕ; шапка — это рама окна, и рама у всех окон одна.
inline void DrawPanelHeader(const ProContext& ctx, const char* label,
                             bool showGear = false, const char* closeKey = nullptr) {
    // Keep every PRO panel on-screen: saved positions from another monitor or a
    // resolution change must not leave windows (half) outside the viewport.
    // Во время DPI-перехода клэмп выключен (см. g_layout_freeze_frames).
    if (g_layout_freeze_frames <= 0)
    {
        const ImGuiViewport* v = ImGui::GetMainViewport();
        ImVec2 ws = ImGui::GetWindowSize(), wp = ImGui::GetWindowPos();
        ImVec2 ns = {fminf(ws.x, v->WorkSize.x), fminf(ws.y, v->WorkSize.y)};
        ImVec2 np = {fminf(fmaxf(wp.x, v->WorkPos.x), v->WorkPos.x + v->WorkSize.x - ns.x),
                     fminf(fmaxf(wp.y, v->WorkPos.y), v->WorkPos.y + v->WorkSize.y - ns.y)};
        if (ns.x != ws.x || ns.y != ws.y) ImGui::SetWindowSize(ns);
        if (np.x != wp.x || np.y != wp.y) ImGui::SetWindowPos(np);
    }
    float       w  = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2      p  = ImGui::GetCursorScreenPos();

    const float hdrH = header_h();
    dl->AddRectFilled(p, {p.x + w, p.y + hdrH}, COL_HDR_BG);
    dl->AddLine({p.x, p.y + hdrH}, {p.x + w, p.y + hdrH}, COL_GOLD_DIM, 1.f);

    // Подпись: один шрифт (Russo One), один кегль в пунктах × DPI, один цвет —
    // как в шапке RELATIVE MAP, взятой за образец. Высота полосы тоже задана в
    // пунктах, поэтому шапка одинакова и на мониторе с другой плотностью.
    ImFont*     lf  = ctx.russo;
    const float fSz = ui_scale::points(UIConfig::FONT_PT_RUSSO_SMALL);
    const float ty  = p.y + (hdrH - fSz) * 0.5f;
    dl->AddText(lf, fSz, {p.x + pad_px() * 0.8f, ty}, COL_HDR_TEXT, label);

    // Крестик закрытия у правого края; шестерёнка (если есть) уходит левее него.
    // Значки тоже в пунктах × DPI: на плотном мониторе крестик, заданный в
    // пикселях, превращался в точку, хотя полоса вокруг него росла.
    const float icoR    = ui_scale::points(4.5f);   // половина крестика
    const float icoStep = ui_scale::points(14.f);   // шаг между значками
    if (showGear) {
        ImVec2 gc = {p.x + w - icoStep - (closeKey ? icoStep : 0.f), p.y + hdrH * 0.5f};
        dl->AddCircle(gc, ui_scale::points(6.f), COL_DIM, 8, ui_scale::points(1.5f));
        dl->AddCircleFilled(gc, ui_scale::points(2.2f), COL_DIM);
    }
    if (closeKey) {
        ImVec2 cc = {p.x + w - icoStep, p.y + hdrH * 0.5f};
        const float pad = ui_scale::points(3.f);
        bool hov = ImGui::IsMouseHoveringRect({cc.x - icoR - pad, cc.y - icoR - pad},
                                              {cc.x + icoR + pad, cc.y + icoR + pad}, false);
        ImU32 xcol = hov ? COL_WHITE : COL_DIM;
        dl->AddLine({cc.x - icoR, cc.y - icoR}, {cc.x + icoR, cc.y + icoR}, xcol, ui_scale::points(1.6f));
        dl->AddLine({cc.x - icoR, cc.y + icoR}, {cc.x + icoR, cc.y - icoR}, xcol, ui_scale::points(1.6f));
        if (hov && ImGui::IsMouseClicked(0)) SetPanelVisible(closeKey, false);
    }

    // Advance cursor past header (always a Dummy — avoids InvisibleButton
    // item-state side-effects that can break subsequent content rendering)
    ImGui::Dummy(ImVec2(w, hdrH + 2.f));

    // Drag: raw rect check so we never touch the ImGui item stack
    if (!g_pro_layout_locked) {
        ImVec2 wp    = ImGui::GetWindowPos();
        ImVec2 hMax  = {wp.x + w, wp.y + hdrH};
        ImVec2 click = ImGui::GetIO().MouseClickedPos[0]; // where LMB was pressed
        bool   startedInHdr = click.x >= wp.x && click.x <= wp.x + w &&
                              click.y >= wp.y && click.y <= wp.y + hdrH;
        if (ImGui::IsMouseHoveringRect(wp, hMax, false))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (startedInHdr && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos({wp.x + d.x, wp.y + d.y});
        }
    }
}

// ── Переключатели в шапке панели ─────────────────────────────────────────────
// Надпись-кнопка, прижатая правым краем к `rightEdge`. Возвращает свой левый
// край (по нему ставится следующая кнопка левее) и сообщает о клике.
//
// Одна точка и на отрисовку, и на попадание мышью: считать прямоугольник дважды
// — верный способ развести их при первой же правке. Надписи серые, как и подпись
// шапки: это служебные переключатели, а не данные. Включённое состояние
// показывает яркость, а не другой цвет — золотом на этом экране выделены
// значения, и спорить с ними шапка не должна. `arrow` дорисовывает треугольник:
// у надписи, за которой открывается список, должен быть признак, что она
// раскрывается.
//
// Звать внутри Begin/End своей панели, после DrawPanelHeader.
float HeaderButton(const ProContext& ctx, const char* label, bool active, bool arrow,
                   float rightEdge, bool& clicked);

// Оформление выпадающего списка панели — ТЕМИ ЖЕ константами, что и меню
// навбара (UIConfig::DROPDOWN_*). Свой набор цветов в панели означал бы два
// разных выпадающих меню в одном приложении, а разойтись им хватило бы одной
// правки палитры. Вызовы обязаны быть парными, между ними — BeginPopup/EndPopup.
void PushDropdownStyle();
void PopDropdownStyle();

// ── Separator line ───────────────────────────────────────────────────────────
inline void DrawSep(float alpha = 1.f) {
    float w = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(p, {p.x + w, p.y}, IM_COL32(48, 48, 48, (int)(alpha * 255)), 1.f);
    ImGui::Dummy(ImVec2(w, 1.f));
}

// ── Label (Russo #515151) + right-aligned value (Ubuntu white) ───────────────
// Call from inside a Begin/End window.
inline void LabelValue(const ProContext& ctx, const char* lbl, const char* val,
                        ImU32 valCol = COL_WHITE) {
    float rightX = ImGui::GetContentRegionMax().x - pad_px();

    ImGui::SetCursorPosX(pad_px());
    if (ctx.russo) ImGui::PushFont(ctx.russo);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(COL_LABEL));
    ImGui::TextUnformatted(lbl);
    ImGui::PopStyleColor();
    if (ctx.russo) ImGui::PopFont();

    // Right-align value: SameLine with absolute X so value never overflows
    float valW = ImGui::CalcTextSize(val).x;
    ImGui::SameLine(rightX - valW);
    if (ctx.regular) ImGui::PushFont(ctx.regular);
    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(valCol));
    ImGui::TextUnformatted(val);
    ImGui::PopStyleColor();
    if (ctx.regular) ImGui::PopFont();
}

// Боковое меню групп с fly-out (правый край). Рисуется поверх панелей и сам
// управляет видимостью. Реализация — ProSidebar.cpp.
void RenderSidebar(const ProContext& ctx, ImVec2 vpSz, float topH, float botH);

// F1-подобная «релативная карта»: гонщики на кольце + относительное время.
// Реализация — ProRelative.cpp.
void RenderRelativeWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH);

// Main entry — called from UI::RenderProView()
void Render(const ProContext& ctx, float swipeAnim);

} // namespace Pro
