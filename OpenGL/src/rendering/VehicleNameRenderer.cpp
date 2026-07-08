#include "VehicleNameRenderer.h"
#include <glm/gtc/type_ptr.hpp>
#include "../../libraries/include/imgui/imgui.h"

namespace VehicleNameRenderer
{

    void DrawName(const std::string& name,
        float worldX, float worldY,
        const glm::mat4& viewProj,
        ImFont* font,
        float textScale)
    {
        if (name.empty()) return;

        glm::vec4 clip = viewProj * glm::vec4(worldX, worldY, 0.0f, 1.0f);
        if (clip.w <= 0.0f) return;

        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f)
            return;

        ImGuiIO& io = ImGui::GetIO();
        float screenX = (ndc.x * 0.5f + 0.5f) * io.DisplaySize.x;
        float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * io.DisplaySize.y;

        std::string label = name.substr(0, 4);

        // Устанавливаем шрифт и масштаб для правильного расчета размеров ImGui::CalcTextSize
        if (font) {
            ImGui::PushFont(font);
        }

        // Применяем временный масштаб текста
        float oldScale = font ? font->Scale : 1.0f;
        if (font) font->Scale = textScale;

        // Расчет размеров текста Russo One с учетом масштаба
        ImVec2 textSize = ImGui::CalcTextSize(label.c_str());

        // ==========================================
        // НАСТРОЙКА ГЕОМЕТРИИ ВЫНОСКИ
        // ==========================================
        float anchorOffsetY = -50.0f; // Смещение начала линии вверх от центра объекта
        float leaderLengthX = 50.0f;  // Длина наклонной линии по горизонтали (вправо)
        float leaderLengthY = -25.0f; // Длина наклонной линии по вертикали (вверх, поэтому минус)
        float linePaddingX = 8.0f;   // На сколько линия длиннее текста в конце
        float lineWidth = 4.5f;   // Толщина линий выноски

        // Цвет текста и линий (светло-голубой / Cyan, как на фото)
        ImU32 mainColor = IM_COL32(255, 255, 255, 255);
        ImU32 shadowColor = IM_COL32(0, 0, 0, 180); // Тень для читаемости на любом фоне

        // Расчет точек линий
        ImVec2 anchorPt = ImVec2(screenX, screenY + anchorOffsetY);
        ImVec2 elbowPt = ImVec2(anchorPt.x + leaderLengthX, anchorPt.y + leaderLengthY);
        ImVec2 endPt = ImVec2(elbowPt.x + textSize.x + linePaddingX, elbowPt.y);
        ImVec2 textPos = ImVec2(elbowPt.x + 2.0f, elbowPt.y - textSize.y - 2.0f);

        // ==========================================
        // ОТРЕНДЕРИНГ
        // ==========================================
        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        // 1. Отрисовка тени (линии + текст)
        dl->AddLine(ImVec2(anchorPt.x + 1.0f, anchorPt.y + 1.0f), ImVec2(elbowPt.x + 1.0f, elbowPt.y + 1.0f), shadowColor, lineWidth);
        dl->AddLine(ImVec2(elbowPt.x + 1.0f, elbowPt.y + 1.0f), ImVec2(endPt.x + 1.0f, endPt.y + 1.0f), shadowColor, lineWidth);
        dl->AddText(font, font ? font->FontSize * textScale : io.FontGlobalScale, ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), shadowColor, label.c_str());

        // 2. Отрисовка основных линий
        dl->AddLine(anchorPt, elbowPt, mainColor, lineWidth);
        dl->AddLine(elbowPt, endPt, mainColor, lineWidth);

        // 3. Отрисовка основного текста шрифтом Russo One
        dl->AddText(font, font ? font->FontSize * textScale : io.FontGlobalScale, textPos, mainColor, label.c_str());

        // Возвращаем масштаб шрифта в исходное состояние и закрываем PushFont
        if (font) {
            font->Scale = oldScale;
            ImGui::PopFont();
        }
    }

} // namespace VehicleNameRenderer