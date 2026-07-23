#include "SettingsPanel.h"

#include <cstring>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>
#include <ctime>

#include <imgui/imgui.h>

#include "UI_Config.h"
#include "../core/DeviceRegistry.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace SettingsPanel {
namespace {

bool s_open          = false;
bool s_focus_pending = false;
bool s_synced        = false;   // подтянули ли записи из реестра после открытия

// Категории левого списка (стиль Blender Preferences). Пока одна — Connected
// Devices; enum оставляет место для будущих разделов без переделки раскладки.
enum class Category { ConnectedDevices };
Category s_category = Category::ConnectedDevices;

// Локальная редактируемая копия строки таблицы. Реестр — источник истины;
// сюда тянем при открытии/обновлении, а правки сразу пишем обратно в реестр.
struct EditRow {
    int32_t id      = 0;
    char    name[64]  = "";
    char    group[64] = "";
    int     id_edit = 0;   // буфер для правки ID
    bool    seen    = false;
    int64_t last_seen = 0;
};
std::vector<EditRow> s_rows;

char s_status_msg[128] = "";   // результат импорта таблицы, показывается над кнопками

void fill_row(EditRow& r, const DeviceInfo& d) {
    r.id = d.device_id;
    r.id_edit = d.device_id;
    std::snprintf(r.name,  sizeof(r.name),  "%s", d.name.c_str());
    std::snprintf(r.group, sizeof(r.group), "%s", d.group.c_str());
    r.seen = d.seen_now;
    r.last_seen = d.last_seen_unix;
}

// Дотягивает из реестра: обновляет статус существующих строк и дописывает новые
// устройства (не трогая буферы, что редактирует пользователь).
void sync_from_registry(bool rebuild) {
    const auto devices = DeviceRegistry::instance().snapshot();
    if (rebuild) s_rows.clear();

    for (const auto& d : devices) {
        auto it = s_rows.end();
        for (auto i = s_rows.begin(); i != s_rows.end(); ++i)
            if (i->id == d.device_id) { it = i; break; }

        if (it == s_rows.end()) {
            EditRow r; fill_row(r, d);
            s_rows.push_back(r);
        } else {
            it->seen = d.seen_now;           // статус/время — только чтение, обновляем
            it->last_seen = d.last_seen_unix;
        }
    }
}

#ifdef _WIN32
// Открывает системный диалог выбора CSV-файла. Пустая строка — отмена.
std::string pick_csv_file() {
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = sizeof(file);
    ofn.lpstrFilter = "CSV table\0*.csv\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? std::string(file) : std::string();
}
#endif

const char* last_seen_text(int64_t unix_seconds, char* buf, size_t n) {
    if (unix_seconds <= 0) { std::snprintf(buf, n, "-"); return buf; }
    std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm_info{};
#ifdef _WIN32
    localtime_s(&tm_info, &t);
#else
    localtime_r(&t, &tm_info);
#endif
    std::strftime(buf, n, "%d/%m %H:%M", &tm_info);
    return buf;
}

} // namespace

void Toggle() {
    s_open = !s_open;
    if (s_open) { s_focus_pending = true; s_synced = false; }
}

bool IsOpen() { return s_open; }

void Render(ImFont* bodyFont, ImFont* boldFont) {
    if (!s_open) return;

    if (!s_synced) { sync_from_registry(/*rebuild=*/true); s_synced = true; }

    ImGuiIO& io      = ImGui::GetIO();
    const ImVec2 dsz = io.DisplaySize;
    const float  mW  = UIConfig::HELP_MODAL_WIDTH  * dsz.x;
    const float  mH  = UIConfig::HELP_MODAL_HEIGHT * dsz.y;
    const ImVec2 mPos((dsz.x - mW) * 0.5f, (dsz.y - mH) * 0.5f);
    const ImVec2 mEnd(mPos.x + mW, mPos.y + mH);

    const ImVec4 colGold(218.f / 255.f, 165.f / 255.f, 64.f / 255.f, 1.f);
    const ImVec4 colDim (0.70f, 0.70f, 0.70f, 1.f);
    const ImU32  uGold  = IM_COL32(218, 165, 64, 255);
    const ImU32  uSep   = IM_COL32(55, 55, 55, 255);

    // ── overlay (клик вне окна закрывает) ────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(dsz);
    ImGui::SetNextWindowBgAlpha(UIConfig::MODAL_OVERLAY_ALPHA);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, UIConfig::MODAL_OVERLAY_ALPHA));
    if (ImGui::Begin("##SettingsOverlay", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        const ImVec2 mp = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(0) &&
            (mp.x < mPos.x || mp.x > mEnd.x || mp.y < mPos.y || mp.y > mEnd.y))
            s_open = false;
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (!s_open) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { s_open = false; return; }

    const float padX   = mW * 0.044f;
    const float padY   = mH * 0.03f;
    const float titleH = mH * 0.06f; // компактная шапка (была 0.11 — ~вдвое ниже)

    ImGui::SetNextWindowPos(mPos);
    ImGui::SetNextWindowSize(ImVec2(mW, mH));
    if (s_focus_pending) { ImGui::SetNextWindowFocus(); s_focus_pending = false; }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,   5.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIConfig::MODAL_BG_R, UIConfig::MODAL_BG_G, UIConfig::MODAL_BG_B, UIConfig::MODAL_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.22f, 0.22f, 0.22f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,   ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.32f, 0.32f, 0.32f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.50f, 0.50f, 0.50f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ImVec4(0.70f, 0.70f, 0.70f, 1.f));

    bool modal_open = true;
    if (ImGui::Begin("##SettingsModal", &modal_open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Заголовок ────────────────────────────────────────────────────────
        const float titleBarBot = mPos.y + titleH;
        dl->AddRectFilled(mPos, ImVec2(mEnd.x, titleBarBot),
            IM_COL32((int)(UIConfig::MODAL_TITLE_BG_R * 255),
                     (int)(UIConfig::MODAL_TITLE_BG_G * 255),
                     (int)(UIConfig::MODAL_TITLE_BG_B * 255), 255),
            10.0f, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(mPos.x, titleBarBot), ImVec2(mEnd.x, titleBarBot), uSep, 1.0f);
        dl->AddRectFilled(ImVec2(mPos.x, mPos.y + 3.f), ImVec2(mPos.x + 4.0f, titleBarBot - 3.f), uGold, 2.0f);
        if (boldFont) ImGui::PushFont(boldFont);
        {
            const char* tTxt = "Settings";
            const ImVec2 tSz = ImGui::CalcTextSize(tTxt);
            dl->AddText(ImVec2(mPos.x + padX + 8.f, mPos.y + (titleH - tSz.y) * 0.5f),
                        IM_COL32(235, 235, 235, 255), tTxt);
        }
        if (boldFont) ImGui::PopFont();

        // Крестик закрытия в шапке (справа сверху).
        {
            const ImVec2 xc(mEnd.x - 24.f, mPos.y + titleH * 0.5f);
            const float  xr = 6.f;
            const bool xhov = ImGui::IsMouseHoveringRect(
                ImVec2(xc.x - xr - 5.f, xc.y - xr - 5.f), ImVec2(xc.x + xr + 5.f, xc.y + xr + 5.f), false);
            const ImU32 xcol = xhov ? IM_COL32(255, 255, 255, 255) : IM_COL32(170, 170, 170, 255);
            dl->AddLine(ImVec2(xc.x - xr, xc.y - xr), ImVec2(xc.x + xr, xc.y + xr), xcol, 2.f);
            dl->AddLine(ImVec2(xc.x - xr, xc.y + xr), ImVec2(xc.x + xr, xc.y - xr), xcol, 2.f);
            if (xhov && ImGui::IsMouseClicked(0)) s_open = false;
        }

        // ── Содержимое: слева категории-плашки, справа контент отдельным блоком ──
        const float contentH = mH - titleH - 2.f * padY;
        const float sideW    = (mW - padX * 2) * 0.24f;
        const float gapW     = padX * 0.6f;
        const float rightW   = (mW - padX * 2) - sideW - gapW;

        ImFont* body = bodyFont ? bodyFont : ImGui::GetFont();
        ImFont* bold = boldFont ? boldFont : body;

        const ImVec4 goldBright(238.f / 255.f, 185.f / 255.f, 84.f / 255.f, 1.f);
        const ImVec4 goldDark  (198.f / 255.f, 145.f / 255.f, 44.f / 255.f, 1.f);

        // Заголовок секции: золотая подпись + подчёркивание на ширину блока.
        auto sectionHeader = [&](const char* label, float underlineW) {
            ImGui::Dummy(ImVec2(0, 2.f));
            ImGui::PushFont(bold);
            ImGui::TextColored(colGold, "%s", label);
            ImGui::PopFont();
            const ImVec2 p = ImGui::GetItemRectMin();
            const ImVec2 q = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, q.y + 3.f),
                ImVec2(p.x + underlineW, q.y + 3.f), IM_COL32(218, 165, 64, 140), 2.f);
            ImGui::Dummy(ImVec2(0, 6.f));
        };

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 10.f));

        // ── Левый столбец: категории как отдельные плашки-«фигуры» ────────────
        ImGui::SetCursorPos(ImVec2(padX, titleH + padY));
        ImGui::BeginChild("##catList", ImVec2(sideW, contentH), false);
        {
            ImGui::PushFont(bold);
            const float blockH = ImGui::GetTextLineHeight() + 20.f;

            // Плашка раздела. active — золотая (выбрана). enabled=false — заготовка
            // будущего раздела: видно, как будет выглядеть, но кликом не выбирается.
            auto catBlock = [&](const char* label, bool active, bool enabled) -> bool {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   6.f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.06f, 0.5f));
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        colGold);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, goldBright);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  goldDark);
                    ImGui::PushStyleColor(ImGuiCol_Border,        goldBright);
                    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.10f, 0.08f, 0.02f, 1.f));
                } else {
                    // Заготовка будущего раздела: неактивна, без hover-подсветки.
                    const float bg = enabled ? 0.19f : 0.16f;
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(bg, bg, bg + 0.01f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(enabled ? 0.24f : bg, enabled ? 0.24f : bg, enabled ? 0.26f : bg + 0.01f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(bg, bg, bg + 0.01f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.28f, 0.28f, 0.30f, 1.f));
                    const float t = enabled ? 0.78f : 0.45f;
                    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(t, t, t + 0.03f, 1.f));
                }
                const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, blockH));
                ImGui::PopStyleColor(5);
                ImGui::PopStyleVar(3);
                return clicked && enabled;
            };

            // Заготовки будущих разделов (неактивны — показывают вид на будущее).
            // "##N" даёт уникальный ID при одинаковой надписи — иначе ImGui считает
            // их одним виджетом (конфликт ID) и рисует красную отладочную подсветку.
            catBlock("Configuration##0", false, false);
            catBlock("Configuration##1", false, false);
            catBlock("Configuration##2", false, false);
            catBlock("Configuration##3", false, false);

            ImGui::Dummy(ImVec2(0, 4.f)); // разрыв перед активным разделом

            if (catBlock("Connected Devices", s_category == Category::ConnectedDevices, true))
                s_category = Category::ConnectedDevices;

            ImGui::PopFont();
        }
        ImGui::EndChild();

        // ── Правый столбец: контент отдельным блоком (панель) ────────────────
        ImGui::SetCursorPos(ImVec2(padX + sideW + gapW, titleH + padY));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f, 14.f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.105f, 0.105f, 0.115f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0.22f, 0.22f, 0.24f, 1.f));
        ImGui::BeginChild("##catContent", ImVec2(rightW, contentH), true);
        ImGui::PushFont(body);

        if (s_category == Category::ConnectedDevices) {
            const float innerW = ImGui::GetContentRegionAvail().x;

            sectionHeader("Connected Devices", innerW);

            // Живые устройства могли появиться после открытия — дотягиваем без сброса правок.
            sync_from_registry(/*rebuild=*/false);

            // Подсказка: реальные строки редактируемы (клик по ячейке), примеры — нет.
            ImGui::TextColored(colDim, "%s", s_rows.empty()
                ? "Example preview below. Press Add to create an editable device, or connect a tracker."
                : "Click Name / Group / ID to edit. Changes are saved to devices.db.");
            ImGui::Dummy(ImVec2(0.f, 2.f));

            // — таблица устройств (пустые строки-заготовки дают вид сетки, как в макете) —
            ImGui::PushStyleColor(ImGuiCol_TableBorderLight,  ImVec4(0.24f, 0.24f, 0.26f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.30f, 0.30f, 0.32f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBg,        ImVec4(0.155f, 0.155f, 0.165f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,     ImVec4(0.135f, 0.135f, 0.145f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg,     ImVec4(0.17f, 0.17f, 0.18f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg,           ImVec4(0.135f, 0.135f, 0.145f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,    ImVec4(0.20f, 0.20f, 0.22f, 1.f));

            int remove_id = 0;
            if (ImGui::BeginTable("Devices", 5,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Device ID", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("Name",      ImGuiTableColumnFlags_WidthStretch, 2.4f);
                ImGui::TableSetupColumn("Group",     ImGuiTableColumnFlags_WidthStretch, 1.9f);
                ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupColumn("##act",     ImGuiTableColumnFlags_WidthFixed,   innerW * 0.14f);
                ImGui::TableHeadersRow();

                for (auto& r : s_rows) {
                    ImGui::TableNextRow();
                    ImGui::PushID(r.id);

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputInt("##id", &r.id_edit, 0, 0);
                    if (ImGui::IsItemDeactivatedAfterEdit() && r.id_edit != r.id && r.id_edit != 0) {
                        DeviceRegistry::instance().set_device_id(r.id, r.id_edit);
                        r.id = r.id_edit;
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##name", r.name, sizeof(r.name)))
                        DeviceRegistry::instance().set_name(r.id, r.name);

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##group", r.group, sizeof(r.group)))
                        DeviceRegistry::instance().set_group(r.id, r.group);

                    ImGui::TableNextColumn();
                    if (r.seen) ImGui::TextColored(ImVec4(0.36f, 0.84f, 0.54f, 1.f), "%s", "Connected");
                    else {
                        char tb[32];
                        ImGui::TextColored(colDim, "seen %s", last_seen_text(r.last_seen, tb, sizeof(tb)));
                    }

                    ImGui::TableNextColumn();
                    // Кнопка удаления в гамме приложения: прозрачный фон, приглушённо-
                    // красный текст, красноватая подсветка при наведении (не синяя дефолтная).
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.f, 0.f, 0.f, 0.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.42f, 0.13f, 0.13f, 0.65f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.52f, 0.11f, 0.11f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.82f, 0.40f, 0.40f, 1.f));
                    if (ImGui::SmallButton("Remove")) remove_id = r.id;
                    ImGui::PopStyleColor(4);

                    ImGui::PopID();
                }

                // Нет реальных устройств → показываем пример данных, чтобы было
                // видно, как таблица выглядит заполненной. Примеры приглушены, не
                // редактируются и не сохраняются; реальные трекеры их заменят.
                if (s_rows.empty()) {
                    struct Demo { int id; const char* name; const char* group; const char* status; bool conn; };
                    static const Demo demo[] = {
                        { 101, "Car 1", "Red",   "seen 14:52", true  },
                        { 102, "Car 2", "Red",   "seen 14:50", true  },
                        { 103, "Car 3", "Blue",  "seen 14:47", false },
                        { 204, "Car 4", "Blue",  "seen 14:41", false },
                        { 205, "Car 5", "Green", "seen 14:39", true  },
                    };
                    for (const auto& d : demo) {
                        ImGui::TableNextRow();
                        char idb[16]; std::snprintf(idb, sizeof(idb), "%d", d.id);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.f));
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(idb);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(d.name);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(d.group);
                        ImGui::TableNextColumn();
                        if (d.conn) ImGui::TextColored(ImVec4(0.34f, 0.62f, 0.44f, 1.f), "Connected");
                        else        ImGui::TextUnformatted(d.status);
                        ImGui::PopStyleColor();
                        ImGui::TableNextColumn(); ImGui::TextDisabled("example");
                    }
                }

                // Пустых строк-заготовок нет: строка появляется только под реальную
                // запись (или пример, когда база пуста).
                ImGui::EndTable();
            }

            if (remove_id != 0) {
                DeviceRegistry::instance().remove_device(remove_id);
                for (auto i = s_rows.begin(); i != s_rows.end(); ++i)
                    if (i->id == remove_id) { s_rows.erase(i); break; }
            }

            ImGui::PopStyleColor(7); // table colors

            if (s_status_msg[0]) ImGui::TextColored(colGold, "%s", s_status_msg);

            // — кнопки внизу справа: Add / Refresh / Load Table, серые (не золотые),
            //   пришпилены к правому нижнему углу блока —
            const float bH   = ImGui::GetTextLineHeight() + 14.f;
            const float bW   = fminf(innerW * 0.20f, 140.f);
            const float bGap = 10.f;
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - bH);
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - bW * 3.f - bGap * 2.f);

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.20f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.28f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.15f, 0.17f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.90f, 0.90f, 0.92f, 1.f));
            ImGui::PushFont(bold);

            // Add — создаёт новую редактируемую строку со свободным ID. Дальше имя,
            // группу и сам ID правят прямо в таблице (реальные строки редактируемы).
            if (ImGui::Button("Add", ImVec2(bW, bH))) {
                int newId = 1;
                for (const auto& r : s_rows) if (r.id >= newId) newId = r.id + 1;
                DeviceRegistry::instance().add_device(newId, "", "");
                sync_from_registry(/*rebuild=*/false);
            }
            ImGui::SameLine(0.f, bGap);
            if (ImGui::Button("Refresh", ImVec2(bW, bH)))
                sync_from_registry(/*rebuild=*/true);
            ImGui::SameLine(0.f, bGap);
            if (ImGui::Button("Load Table", ImVec2(bW, bH))) {
#ifdef _WIN32
                const std::string path = pick_csv_file();
                if (!path.empty()) {
                    auto n = DeviceRegistry::instance().import_csv(path);
                    if (n) std::snprintf(s_status_msg, sizeof(s_status_msg), "Imported %d row(s).", *n);
                    else   std::snprintf(s_status_msg, sizeof(s_status_msg), "Failed to read file.");
                    sync_from_registry(/*rebuild=*/true);
                }
#else
                std::snprintf(s_status_msg, sizeof(s_status_msg), "File dialog available on Windows only.");
#endif
            }

            ImGui::PopFont();
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
        }

        ImGui::PopFont();        // body (правая панель)
        ImGui::EndChild();       // ##catContent
        ImGui::PopStyleColor(2); // ChildBg + Border
        ImGui::PopStyleVar(2);   // ChildRounding + WindowPadding
        ImGui::PopStyleVar();    // ItemSpacing
    }
    ImGui::End();

    if (!modal_open) s_open = false;

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

} // namespace SettingsPanel
