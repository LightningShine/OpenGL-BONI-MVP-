#include "ProView.h"
#include <imgui.h>
#include <unordered_map>
#include <string>
#include <fstream>
#include <cstring>

// Боковое меню PRO-вида в стиле McLaren: узкая полоса иконок у правого края,
// при наведении на иконку группы выпадает fly-out со списком её панелей. Клик
// по пункту показывает/прячет соответствующее плавающее окно. Видимость каждой
// панели персистится в pro_panels.ini.

namespace Pro {

// ── Видимость панелей (персист в pro_panels.ini) ─────────────────────────────
static std::unordered_map<std::string, bool> g_visible;
static bool g_visLoaded = false;
static const char* kVisFile = "pro_panels.ini";

// Значение по умолчанию, если ключа ещё нет в файле. Все существующие панели
// показаны, релативная карта скрыта — чтобы не менять привычный вид с ходу.
static bool defaultVisible(const std::string& key) {
    return key != "Relative";
}

static void loadVis() {
    if (g_visLoaded) return;
    g_visLoaded = true;
    std::ifstream f(kVisFile);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        g_visible[line.substr(0, eq)] = (line[eq + 1] == '1');
    }
}

static void saveVis() {
    std::ofstream f(kVisFile, std::ios::trunc);
    if (!f) return;
    for (auto& [k, v] : g_visible) f << k << "=" << (v ? 1 : 0) << "\n";
}

bool PanelVisible(const char* key) {
    loadVis();
    auto it = g_visible.find(key);
    return (it != g_visible.end()) ? it->second : defaultVisible(key);
}

void SetPanelVisible(const char* key, bool v) {
    loadVis();
    g_visible[key] = v;
    saveVis();
}

void TogglePanel(const char* key) { SetPanelVisible(key, !PanelVisible(key)); }

// ── Определение групп бокового меню ──────────────────────────────────────────
struct PanelItem { const char* key; const char* label; };
struct MenuGroup { const char* title; int icon; const PanelItem* items; int count; };

// Подписи английские — атлас шрифтов грузится без кириллицы, как и вся PRO-панель.
static const PanelItem GRP_INFO[] = {
    { "LapList",     "Lap List"     },   // информация о кругах
    { "LapInfo",     "Lap Info"     },   // информация о круге
    { "SessionInfo", "Session Info" },   // информация о сессии
    { "Events",      "Events / Log" },   // логи
};
static const PanelItem GRP_MAP[] = {
    { "TrackMap", "Track Map"    },      // карта
    { "Sectors",  "Sectors"      },      // секторы
    { "Relative", "Relative Map" },      // релативная карта (F1)
};
static const PanelItem GRP_DATA[] = {
    { "Laptime",  "Laptime"  },          // время круга
    { "GForce",   "G-Force"  },          // перегрузки
    { "Channels", "Channels" },          // каналы/графики
};

static const MenuGroup GROUPS[] = {
    { "INFO",       0, GRP_INFO, 4 },
    { "MAP",        1, GRP_MAP,  3 },
    { "DATA",       2, GRP_DATA, 3 },
};
static const int GROUP_COUNT = 3;

// Размер fly-out группы (ширина по самой длинной подписи, высота по числу строк).
// rowH/hdrH возвращаются наружу, чтобы отрисовка и раскладка совпадали.
static ImVec2 flyoutSize(const ProContext& ctx, const MenuGroup& g,
                         float& rowH, float& hdrH) {
    ImFont* f   = ctx.regular ? ctx.regular : ImGui::GetFont();
    float   fSz = f->FontSize;
    rowH = fSz + 12.f;
    hdrH = fSz + 10.f;
    const float padX = 12.f, dot = 6.f;
    float maxW = f->CalcTextSizeA(fSz, FLT_MAX, 0.f, g.title).x;
    for (int i = 0; i < g.count; ++i)
        maxW = fmaxf(maxW, f->CalcTextSizeA(fSz, FLT_MAX, 0.f, g.items[i].label).x);
    float menuW = maxW + padX * 3.f + dot * 2.f + 18.f;
    float menuH = hdrH + rowH * g.count + 8.f;
    return { menuW, menuH };
}

// ── Fly-out меню для одной группы ────────────────────────────────────────────
// Позиция/размер уже посчитаны вызывающим (для геометрической проверки наведения).
static void drawFlyout(const ProContext& ctx, const MenuGroup& g,
                       ImVec2 pos, ImVec2 size, float rowH, float hdrH) {
    ImFont* f    = ctx.regular ? ctx.regular : ImGui::GetFont();
    float   fSz  = f->FontSize;
    const float padX = 12.f, dot = 6.f;
    float menuW = size.x;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(22, 22, 22, 252));
    ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(0xDA, 0xA5, 0x40, 120));

    char id[32]; snprintf(id, sizeof(id), "##flyout_%d", g.icon);
    ImGui::Begin(id, nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();

    // Заголовок группы — золотистый, Russo.
    ImFont* tf = ctx.russo ? ctx.russo : f;
    dl->AddText(tf, tf->FontSize, { wp.x + padX, wp.y + 5.f }, IM_COL32(0xDA, 0xA5, 0x40, 255), g.title);
    dl->AddLine({ wp.x, wp.y + hdrH }, { wp.x + menuW, wp.y + hdrH }, IM_COL32(0xDA, 0xA5, 0x40, 90), 1.f);

    // Строки-переключатели.
    for (int i = 0; i < g.count; ++i) {
        const PanelItem& it = g.items[i];
        float ry = wp.y + hdrH + 4.f + i * rowH;
        ImVec2 rmin = { wp.x, ry }, rmax = { wp.x + menuW, ry + rowH };
        bool hov = ImGui::IsMouseHoveringRect(rmin, rmax, false);
        bool on  = PanelVisible(it.key);

        if (hov) dl->AddRectFilled(rmin, rmax, IM_COL32(0x2E, 0x2E, 0x2E, 255));

        // Индикатор-точка: золото = включено, тёмная = выключено.
        ImVec2 dc = { wp.x + padX + dot, ry + rowH * 0.5f };
        if (on) dl->AddCircleFilled(dc, dot, IM_COL32(0xDA, 0xA5, 0x40, 255));
        else    dl->AddCircle(dc, dot, IM_COL32(0x60, 0x60, 0x60, 255), 12, 1.4f);

        ImU32 tc = on ? IM_COL32(0xF0, 0xF0, 0xF0, 255)
                      : (hov ? IM_COL32(0xC0, 0xC0, 0xC0, 255) : IM_COL32(0x8A, 0x8A, 0x8A, 255));
        dl->AddText(f, fSz, { wp.x + padX * 2.f + dot * 2.f, ry + (rowH - fSz) * 0.5f }, tc, it.label);

        if (hov && ImGui::IsMouseClicked(0)) TogglePanel(it.key);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void RenderSidebar(const ProContext& ctx, ImVec2 vpSz, float topH, float botH) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float STRIP_W = 46.f;
    const float ICON_SZ = 30.f;
    const float ICON_GAP = 14.f;

    float stripH = vpSz.y - topH - botH;
    ImVec2 stripPos = { vp->Pos.x + vpSz.x - STRIP_W, vp->Pos.y + topH };

    ImGui::SetNextWindowPos(stripPos);
    ImGui::SetNextWindowSize({ STRIP_W, stripH });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(16, 16, 16, 235));
    ImGui::Begin("##ProSidebar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();

    // Тонкая золотистая грань слева от полосы.
    dl->AddLine({ wp.x, wp.y }, { wp.x, wp.y + stripH }, IM_COL32(0xDA, 0xA5, 0x40, 60), 1.f);

    // Какая группа «раскрыта» в этом кадре: над иконкой или над её fly-out.
    static int s_openGroup = -1;
    int hoveredIcon = -1;

    // Иконки начинаются сверху полосы (не по центру).
    float startY = wp.y + 14.f;

    ImVec2 iconAnchor[GROUP_COUNT];
    for (int i = 0; i < GROUP_COUNT; ++i) {
        float iy = startY + i * (ICON_SZ + ICON_GAP);
        float ix = wp.x + (STRIP_W - ICON_SZ) * 0.5f;
        ImVec2 imin = { ix, iy }, imax = { ix + ICON_SZ, iy + ICON_SZ };
        iconAnchor[i] = { wp.x, iy + ICON_SZ * 0.5f };

        bool hov = ImGui::IsMouseHoveringRect(imin, imax, false);
        bool open = (s_openGroup == i);
        if (hov) hoveredIcon = i;

        // Подсветка активной/наведённой иконки.
        if (hov || open)
            dl->AddRectFilled({ imin.x - 5.f, imin.y - 5.f }, { imax.x + 5.f, imax.y + 5.f },
                              IM_COL32(0x2A, 0x2A, 0x2A, 255), 4.f);

        void* tex = ctx.numTex[GROUPS[i].icon];
        if (tex) {
            ImU32 tint = (hov || open) ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 190, 190, 255);
            dl->AddImage((ImTextureID)(intptr_t)tex, imin, imax, { 0, 0 }, { 1, 1 }, tint);
        } else {
            // Фолбэк, если PNG не загрузился: рисуем номер группы.
            char nb[4]; snprintf(nb, sizeof(nb), "%d", GROUPS[i].icon + 1);
            ImFont* tf = ctx.russo ? ctx.russo : ImGui::GetFont();
            float tw = tf->CalcTextSizeA(tf->FontSize, FLT_MAX, 0.f, nb).x;
            dl->AddText(tf, tf->FontSize, { imin.x + (ICON_SZ - tw) * 0.5f, imin.y + 4.f },
                        IM_COL32(210, 210, 210, 255), nb);
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Fly-out рисуется после полосы (поверх). Открыт, пока курсор над иконкой либо
    // над самим меню. Проверка наведения — ГЕОМЕТРИЧЕСКАЯ: зона тянется от левого
    // края меню до правого края полосы (мостик), так что при переводе мыши с иконки
    // на список нет «мёртвого» зазора, где меню закрывалось бы.
    int drawGroup = hoveredIcon >= 0 ? hoveredIcon : s_openGroup;
    if (drawGroup >= 0) {
        float rowH = 0.f, hdrH = 0.f;
        ImVec2 fsz = flyoutSize(ctx, GROUPS[drawGroup], rowH, hdrH);
        ImVec2 anchor = iconAnchor[drawGroup];      // { левый край полосы, центр иконки Y }
        ImVec2 pos = { anchor.x - fsz.x, anchor.y - fsz.y * 0.5f };
        if (pos.y < vp->WorkPos.y + 4.f) pos.y = vp->WorkPos.y + 4.f;
        if (pos.y + fsz.y > vp->WorkPos.y + vp->WorkSize.y - 4.f)
            pos.y = vp->WorkPos.y + vp->WorkSize.y - fsz.y - 4.f;

        // Зона удержания: [левый край меню .. правый край полосы] × высота меню.
        ImVec2 m = ImGui::GetIO().MousePos;
        bool overFlyout = m.x >= pos.x && m.x <= wp.x + STRIP_W &&
                          m.y >= pos.y && m.y <= pos.y + fsz.y;

        drawFlyout(ctx, GROUPS[drawGroup], pos, fsz, rowH, hdrH);

        if (hoveredIcon >= 0 || overFlyout) s_openGroup = drawGroup;
        else                                s_openGroup = -1;
    } else {
        s_openGroup = -1;
    }
}

} // namespace Pro
