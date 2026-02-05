#include "UI_Elements.h"
#include "src/ui/UI_Elements_Config.h"
#include "src/ui/UI_Config.h"
#include "src/input/Input.h"
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "libraries/include/imgui/imgui.h"

UIElements::UIElements()
    : m_font_title(nullptr)
    , m_compass_texture(nullptr)
{
}

UIElements::~UIElements()
{
    shutdown();
}

bool UIElements::initialize()
{
    return true;
}

void UIElements::shutdown()
{
    // Resources are managed by main UI class
}

// ============================================================================
// COMPASS with Fixed Aspect Ratio
// ============================================================================

void UIElements::drawCompass(float camera_yaw, const MapOrigin& origin)
{
    if (!m_compass_texture)
        return;
    
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;
    
    // === FIXED ASPECT RATIO SCALING ===
    // Original compass texture: 278x246 px
    static constexpr float COMPASS_TEXTURE_WIDTH = 278.0f;
    static constexpr float COMPASS_TEXTURE_HEIGHT = 246.0f;
	static constexpr float COMPASS_ASPECT_RATIO = COMPASS_TEXTURE_WIDTH / COMPASS_TEXTURE_HEIGHT; // ~1.13
    
    // Calculate compass size maintaining aspect ratio
    const float base_size = display_size.y * UIElementsConfig::Compass::HEIGHT_RATIO;
    const float compass_width = base_size * COMPASS_ASPECT_RATIO;  // Maintain aspect ratio
    const float compass_height = base_size;
    const float spacing = display_size.x * UIElementsConfig::Compass::SPACING_RATIO;
    
    // Position in bottom-left corner (above bottom menu)
    ImVec2 compass_pos = ImVec2(spacing, 
                                display_size.y - compass_height - UIConfig::BOTTOM_MENU_HEIGHT * display_size.y - spacing);
    
    ImGui::SetNextWindowPos(compass_pos);
    ImGui::SetNextWindowSize(ImVec2(compass_width, compass_height));
    ImGui::SetNextWindowBgAlpha(0.0f);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    ImGui::Begin("##Compass", nullptr, flags);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Center of the compass rectangle
    ImVec2 center = ImVec2(compass_pos.x + compass_width * 0.5f, 
                          compass_pos.y + compass_height * 0.5f);
    
    // Draw compass image (static, no rotation)
    ImVec2 compass_min = compass_pos;
    ImVec2 compass_max = ImVec2(compass_pos.x + compass_width, compass_pos.y + compass_height);
    draw_list->AddImage((ImTextureID)m_compass_texture, compass_min, compass_max, 
                       ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
    
    // Yellow triangle indicator (rotates based on camera_yaw)
    const float triangle_width = display_size.x * UIElementsConfig::Compass::TRIANGLE_WIDTH_RATIO;
    const float triangle_height = display_size.y * UIElementsConfig::Compass::TRIANGLE_HEIGHT_RATIO;
    const float arrow_offset_from_top = display_size.y * UIElementsConfig::Compass::ARROW_OFFSET_RATIO;
    
    // Calculate rotation (positive camera_yaw rotates triangle clockwise)
    float rotation_radians = glm::radians(-camera_yaw);
    
    // Distance from center to arrow tip
    float radius = compass_height * 0.5f - arrow_offset_from_top;
    
    // Triangle vertices relative to center (before rotation)
    ImVec2 tip_offset = ImVec2(0, -radius);
    ImVec2 left_offset = ImVec2(-triangle_width * 0.5f, -radius + triangle_height);
    ImVec2 right_offset = ImVec2(triangle_width * 0.5f, -radius + triangle_height);
    
    // Rotate triangle around center
    float cos_a = cosf(rotation_radians);
    float sin_a = sinf(rotation_radians);
    
    ImVec2 arrow_tip = ImVec2(
        center.x + (tip_offset.x * cos_a - tip_offset.y * sin_a),
        center.y + (tip_offset.x * sin_a + tip_offset.y * cos_a)
    );
    
    ImVec2 arrow_left = ImVec2(
        center.x + (left_offset.x * cos_a - left_offset.y * sin_a),
        center.y + (left_offset.x * sin_a + left_offset.y * cos_a)
    );
    
    ImVec2 arrow_right = ImVec2(
        center.x + (right_offset.x * cos_a - right_offset.y * sin_a),
        center.y + (right_offset.x * sin_a + right_offset.y * cos_a)
    );
    
    draw_list->AddTriangleFilled(
        arrow_tip, arrow_left, arrow_right, 
        IM_COL32(UIElementsConfig::Compass::TRIANGLE_COLOR_R,
                UIElementsConfig::Compass::TRIANGLE_COLOR_G,
                UIElementsConfig::Compass::TRIANGLE_COLOR_B,
                UIElementsConfig::Compass::TRIANGLE_COLOR_A)
    );
    
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// LAPTIMER
// ============================================================================

void UIElements::drawLapTimer(float current_lap_time, float last_lap_time, 
                              float best_lap_time, float time_diff)
{
    if (!m_font_title)
        return;
    
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;
    
    // Calculate window size
    const float window_width = display_size.x * UIElementsConfig::LapTimer::WIDTH_RATIO;
    const float window_height = display_size.y * UIElementsConfig::LapTimer::HEIGHT_RATIO;
    
    // Position: top-left, under top menu bar, no spacing
    // TOP_MENU_HEIGHT is a ratio, need to multiply by display height
    const float top_menu_height = UIConfig::TOP_MENU_HEIGHT * display_size.y;
    ImVec2 window_pos = ImVec2(0, top_menu_height);
    
    ImGui::SetNextWindowPos(window_pos);
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
    ImGui::SetNextWindowBgAlpha(1.0f);  // Opaque background
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f));
    
    ImGui::Begin("##LapTimer", nullptr, flags);
    
    ImGui::PushFont(m_font_title);
    
    // Calculate sizes
    const float title_size = display_size.y * UIElementsConfig::LapTimer::TITLE_SIZE_RATIO;
    const float main_time_size = display_size.y * UIElementsConfig::LapTimer::MAIN_TIME_SIZE_RATIO;
    const float label_size = display_size.y * UIElementsConfig::LapTimer::LABEL_SIZE_RATIO;
    const float time_size = display_size.y * UIElementsConfig::LapTimer::TIME_SIZE_RATIO;
    const float left_padding = display_size.x * UIElementsConfig::LapTimer::LEFT_PADDING_RATIO;
    const float top_spacing = display_size.y * UIElementsConfig::LapTimer::TOP_SPACING_RATIO;
    const float element_spacing = display_size.y * UIElementsConfig::LapTimer::ELEMENT_SPACING_RATIO;
    const float line_width = display_size.x * UIElementsConfig::LapTimer::LINE_WIDTH_RATIO;
    
    // Colors
    ImVec4 label_color = ImVec4(UIElementsConfig::LapTimer::LABEL_COLOR_R,
                                UIElementsConfig::LapTimer::LABEL_COLOR_G,
                                UIElementsConfig::LapTimer::LABEL_COLOR_B, 1.0f);
    ImVec4 time_color = ImVec4(UIElementsConfig::LapTimer::TIME_COLOR_R,
                              UIElementsConfig::LapTimer::TIME_COLOR_G,
                              UIElementsConfig::LapTimer::TIME_COLOR_B, 1.0f);
    
    float current_y = top_spacing;
    float row_height = 0.0f;      // For vertical alignment
    float label_offset_y = 0.0f;  // For centering labels
    
    // === LAPTIME (title) ===
    ImGui::SetCursorPos(ImVec2(left_padding, current_y));
    ImGui::SetWindowFontScale(title_size / ImGui::GetFontSize());
    ImGui::TextColored(label_color, "LAPTIME");
    ImGui::SetWindowFontScale(1.0f);
    
    current_y += title_size;
    
    // === Main time (00:39.279) ===
    ImGui::SetCursorPos(ImVec2(left_padding, current_y));
    ImGui::SetWindowFontScale(main_time_size / ImGui::GetFontSize());
    
    // Format current lap time
    char time_buffer[32];
    int minutes = static_cast<int>(current_lap_time / 60.0f);
    float seconds = fmodf(current_lap_time, 60.0f);
    
    // Dynamic formatting: "00:39.279" when < 60s, "1:34.150" when >= 60s
    if (minutes == 0)
        snprintf(time_buffer, sizeof(time_buffer), "00:%06.3f", seconds);
    else
        snprintf(time_buffer, sizeof(time_buffer), "%d:%06.3f", minutes, seconds);
    
    ImGui::TextColored(time_color, "%s", time_buffer);
    ImGui::SetWindowFontScale(1.0f);
    
    current_y += main_time_size + element_spacing;
    
    // === Separator line ===
    drawSeparatorLine(window_width * 0.5f, current_y, line_width, display_size.y);
    
    current_y += element_spacing;
    
    // === LAST LAP ===
    float row_start_y = current_y;
    
    // Label
    ImGui::SetCursorPos(ImVec2(left_padding, row_start_y));
    ImGui::SetWindowFontScale(label_size / ImGui::GetFontSize());
    ImGui::TextColored(label_color, "LAST LAP");
    ImGui::SetWindowFontScale(1.0f);
    
    // Time (aligned to same baseline)
    ImGui::SetCursorPos(ImVec2(left_padding + window_width * 0.35f, row_start_y));
    ImGui::SetWindowFontScale(time_size / ImGui::GetFontSize());
    
    if (last_lap_time >= 0.0f)  // Check if data exists (-1 = no data)
    {
        int last_min = static_cast<int>(last_lap_time / 60.0f);
        float last_sec = fmodf(last_lap_time, 60.0f);
        
        // Dynamic formatting: "00:39.279" when < 60s, "1:34.150" when >= 60s
        if (last_min == 0)
            snprintf(time_buffer, sizeof(time_buffer), "00:%06.3f", last_sec);
        else
            snprintf(time_buffer, sizeof(time_buffer), "%d:%06.3f", last_min, last_sec);
            
        ImGui::TextColored(time_color, "%s", time_buffer);
    }
    else
    {
        ImGui::TextColored(time_color, "--:--:---");
    }
    ImGui::SetWindowFontScale(1.0f);
    
    current_y += row_height + element_spacing;
    
	// There not needs to pe a seperator line here, so we just move to the next element
    
    current_y += element_spacing;
    
    // === BEST LAP ===
    row_height = time_size;
    label_offset_y = (row_height - label_size) * 0.5f;
    
    // Label (vertically centered)
    ImGui::SetCursorPos(ImVec2(left_padding, current_y + label_offset_y));
    ImGui::SetWindowFontScale(label_size / ImGui::GetFontSize());
    ImGui::TextColored(label_color, "BEST LAP");
    ImGui::SetWindowFontScale(1.0f);
    
    // Time (baseline aligned)
    ImGui::SetCursorPos(ImVec2(left_padding + window_width * 0.35f, current_y));
    ImGui::SetWindowFontScale(time_size / ImGui::GetFontSize());
    
    if (best_lap_time >= 0.0f)  // Check if data exists (-1 = no data)
    {
        int best_min = static_cast<int>(best_lap_time / 60.0f);
        float best_sec = fmodf(best_lap_time, 60.0f);
        
        // Dynamic formatting: "00:39.279" when < 60s, "1:34.150" when >= 60s
        if (best_min == 0)
            snprintf(time_buffer, sizeof(time_buffer), "00:%06.3f", best_sec);
        else
            snprintf(time_buffer, sizeof(time_buffer), "%d:%06.3f", best_min, best_sec);
            
        ImGui::TextColored(time_color, "%s", time_buffer);
    }
    else
    {
        ImGui::TextColored(time_color, "--:--:---");
    }
    ImGui::SetWindowFontScale(1.0f);
    
    current_y += row_height + element_spacing;
    
    // === Separator line ===
    drawSeparatorLine(window_width * 0.5f, current_y, line_width, display_size.y);
    
    current_y += element_spacing;
    
    // === TIME DIFF ===
    row_height = time_size;
    label_offset_y = (row_height - label_size) * 0.5f;
    
    // Label (vertically centered)
    ImGui::SetCursorPos(ImVec2(left_padding, current_y + label_offset_y));
    ImGui::SetWindowFontScale(label_size / ImGui::GetFontSize());
    ImGui::TextColored(label_color, "TIME DIFF");
    ImGui::SetWindowFontScale(1.0f);
    
    // Time difference (baseline aligned)
    ImGui::SetCursorPos(ImVec2(left_padding + window_width * 0.35f, current_y));
    ImGui::SetWindowFontScale(time_size / ImGui::GetFontSize());
    
    if (time_diff != 0.0f)
    {
        snprintf(time_buffer, sizeof(time_buffer), "%+.3f", time_diff);
        ImVec4 diff_color = (time_diff < 0.0f) ? ImVec4(240.0f / 255.0f, 240.0f / 255.0f, 240.0f / 255.0f, 1.0f) : ImVec4(240.0f / 255.0f, 240.0f / 255.0f, 240.0f / 255.0f, 1.0f);
        ImGui::TextColored(diff_color, "%s", time_buffer);
    }
    else
    {
        ImGui::TextColored(time_color, "---");
    }
    ImGui::SetWindowFontScale(1.0f);
    
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// HELPER METHODS
// ============================================================================

void UIElements::drawSeparatorLine(float center_x, float y_position, 
                                   float line_width, float display_height)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 window_pos = ImGui::GetWindowPos();
    
    // Line starts from left padding (40px at 1600x900)
    float left_padding = (40.0f / 1600.0f) * display_height * (16.0f / 9.0f); // Convert height to width scale
    float line_start_x = window_pos.x + left_padding;
    float line_end_x = line_start_x + line_width;
    float line_y = window_pos.y + y_position;
    
    ImU32 line_color = IM_COL32(
        static_cast<int>(UIElementsConfig::LapTimer::LINE_COLOR_R * 255),
        static_cast<int>(UIElementsConfig::LapTimer::LINE_COLOR_G * 255),
        static_cast<int>(UIElementsConfig::LapTimer::LINE_COLOR_B * 255),
        255
    );
    
    draw_list->AddLine(
        ImVec2(line_start_x, line_y),
        ImVec2(line_end_x, line_y),
        line_color,
        UIElementsConfig::LapTimer::LINE_THICKNESS
    );
}
