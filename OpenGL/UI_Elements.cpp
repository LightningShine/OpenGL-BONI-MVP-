#include "UI_Elements.h"
#include "src/ui/UI_Elements_Config.h"
#include "src/ui/UI_Config.h"
#include "src/input/Input.h"
#include "src/rendering/Interpolation.h"  // For SplinePoint
#include "src/racing/RaceManager.h"  // For RaceManager and VehicleStanding
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>  // For glm::ortho, glm::translate
#include "libraries/include/imgui/imgui.h"

UIElements::UIElements()
    : m_font_title(nullptr)
    , m_font_roboto_mono(nullptr)
    , m_font_oswald(nullptr)
    , m_font_oswald_bold(nullptr)
    , m_font_jetbrains_mono(nullptr)
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
                              float best_lap_time, float time_diff,
                              int current_lap, int target_laps)
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
    if (target_laps > 0 && current_lap > 0)
    {
        char lap_buf[64];
        snprintf(lap_buf, sizeof(lap_buf), "LAP %d / %d", current_lap, target_laps);
        ImGui::TextColored(label_color, lap_buf);
    }
    else
    {
        ImGui::TextColored(label_color, "LAPTIME");
    }
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

// ============================================================================
// START/FINISH TEXT (Fixed Parallax with Proper MVP Transformation)
// ============================================================================

void UIElements::RenderStartFinishText(float camera_zoom, const glm::vec2& camera_pos, 
                                       float window_width, float window_height)
{
    // Require smooth track points (defined externally in main.cpp)
    extern std::vector<SplinePoint> g_smooth_track_points;
    extern std::atomic<bool> g_is_map_loaded;
    
    if (!g_is_map_loaded || g_smooth_track_points.empty())
        return;
    
    // ========================================================================
    // LEVEL OF DETAIL: Don't render if too zoomed out
    // ========================================================================
    const float BASE_FONT_SIZE = 24.0f;
    float fontSize = BASE_FONT_SIZE * camera_zoom;
    
    // Clamp font size between 10px (min readable) and 100px (max)
    fontSize = glm::clamp(fontSize, 10.0f, 100.0f);
    
    // LOD: Skip rendering if text would be < 8px (unreadable)
    if (fontSize < 8.0f)
        return;
    
    // ========================================================================
    // PROPER MVP TRANSFORMATION (CRITICAL FIX)
    // ========================================================================
    const SplinePoint& firstPoint = g_smooth_track_points[0];
    glm::vec3 worldPos(firstPoint.position.x, firstPoint.position.y, 0.0f);
    
    // Build MVP matrix (same as rendering pipeline)
    // 1. Projection (orthographic)
    float aspectRatio = window_width / window_height;
    float horizontalBound, verticalBound;
    
    if (aspectRatio >= 1.0f) {
        horizontalBound = 1.0f * aspectRatio;
        verticalBound = 1.0f;
    } else {
        horizontalBound = 1.0f;
        verticalBound = 1.0f / aspectRatio;
    }
    
    float zoomedHorizontal = horizontalBound / camera_zoom;
    float zoomedVertical = verticalBound / camera_zoom;
    
    glm::mat4 projection = glm::ortho(
        -zoomedHorizontal,  // left
        zoomedHorizontal,   // right
        -zoomedVertical,    // bottom
        zoomedVertical,     // top
        -1.0f, 1.0f
    );
    
    // 2. View (camera transform)
    glm::mat4 view = glm::mat4(1.0f);
    view = glm::translate(view, glm::vec3(-camera_pos.x, -camera_pos.y, 0.0f));
    
    // 3. Combined MVP
    glm::mat4 mvp = projection * view;
    
    // Transform world position to clip space
    glm::vec4 clipSpace = mvp * glm::vec4(worldPos, 1.0f);
    
    // Perspective divide (for orthographic, w=1.0, but still correct)
    if (clipSpace.w <= 0.0f)
        return;  // Behind camera (shouldn't happen in ortho, but safety check)
    
    glm::vec3 ndc = glm::vec3(clipSpace) / clipSpace.w;
    
    // Convert NDC [-1, 1] to screen pixels [0, window_size]
    float pixelX = (ndc.x + 1.0f) * 0.5f * window_width;
    float pixelY = (1.0f - ndc.y) * 0.5f * window_height;  // Correct Y inversion
    
    // ========================================================================
    // FRUSTUM CULLING: Don't render if off-screen (with margin)
    // ========================================================================
    const float MARGIN = 200.0f;
    if (pixelX < -MARGIN || pixelX > window_width + MARGIN ||
        pixelY < -MARGIN || pixelY > window_height + MARGIN)
        return;
    
    // ========================================================================
    // RENDER TEXT (Properly Centered)
    // ========================================================================
    
    // Create transparent background window (renders first, behind menus)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
    ImGui::SetNextWindowBgAlpha(0.0f);  // Fully transparent
    
    ImGui::Begin("##WorldTextLayer", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Use title font if available, otherwise default font
    ImFont* font = m_font_title ? m_font_title : ImGui::GetFont();
    
    const char* text = "START / FINISH";
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    
    // **CRITICAL FIX**: Center text around anchor point
    ImVec2 finalPos(
        pixelX - textSize.x * 0.5f,  // Horizontal center
        pixelY - textSize.y - 15.0f  // Above line
    );
    
    // Drop shadow (2px offset, semi-transparent black)
    drawList->AddText(font, fontSize, 
                     ImVec2(finalPos.x + 2, finalPos.y + 2),
                     IM_COL32(0, 0, 0, 200),
                     text);
    
    // Main text (white, fully opaque)
    drawList->AddText(font, fontSize, finalPos,
                     IM_COL32(255, 255, 255, 255),
                     text);
    
    ImGui::End();
}

// ============================================================================
// LEADERBOARD - F1-style with POS | DRIVER | TIME/GAP columns
// ============================================================================

void UIElements::drawLeaderboard()
{
    extern RaceManager* g_race_manager;
    extern int g_focused_vehicle_id;

    if (!g_race_manager)
        return;

    std::vector<VehicleStanding> standings = g_race_manager->GetStandings();
    if (standings.empty())
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;

    // =========================================================
    // RESPONSIVE SCALING
    // w_scale drives layout (panel width, column widths, position)
    // ui_scale drives font sizes and row heights — uses the smaller
    // axis ratio so the panel never overflows on tall/narrow displays
    // =========================================================
    const float w_scale  = display_size.x / 1600.0f;
    const float h_scale  = display_size.y / 900.0f;
    const float ui_scale = (w_scale < h_scale) ? w_scale : h_scale;

    // =========================================================
    // COLUMN WIDTHS  (base values at 1600px width)
    // =========================================================
    const float col_pos    = 52.0f  * w_scale;
    const float col_driver = 96.0f  * w_scale;
    const float col_gap    = 96.0f * w_scale;
    const float panel_w    = col_pos + col_driver + col_gap;

    // Row heights  (scale with ui_scale, not w_scale)
    const float header_row_h = 44.0f * ui_scale;
    const float col_header_h = 32.0f * ui_scale;
    const float row_h        = 40.0f * ui_scale;

    const int max_rows = static_cast<int>(standings.size());
    const float panel_h = header_row_h + col_header_h + row_h * max_rows;

    // Position: top-right with small margin
    const float margin_right = 12.0f * w_scale;
    const float margin_top   = display_size.y * 0.05f;
    const float panel_x = display_size.x - panel_w - margin_right;
    const float panel_y = margin_top;

    // Colors
    const ImU32 col_bg         = IM_COL32(0x18, 0x18, 0x18, 255);
    const ImU32 col_header_bg  = IM_COL32(0x18, 0x18, 0x18, 255);
    const ImU32 col_divider    = IM_COL32(0x3A, 0x3A, 0x3A, 255);
    const ImU32 col_text       = IM_COL32(0xD5, 0xD5, 0xD5, 255);
    const ImU32 col_gold       = IM_COL32(0xDA, 0xA5, 0x40, 255);
    const ImU32 col_focus_bg   = IM_COL32(0x2B, 0x2B, 0x2B, 255);
    const ImU32 col_lapped     = IM_COL32(0xF3, 0xCE, 0x87, 255);
    const ImU32 col_lap_accent = IM_COL32(0x18, 0x18, 0x18, 255);

    // Font sizes (all driven by ui_scale for consistent proportions)
    const float fs_header = 26.0f * ui_scale; // "Current Lap X"
    const float fs_col    = 17.0f * ui_scale; // column label (POS/DRIVER/TIME/GAP)
    const float fs_data   = 25.0f * ui_scale; // driver name & gap

    const float corner_r = 8.0f * ui_scale;

    // Window setup
    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y));
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, corner_r);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##Leaderboard2", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Draw full panel background with rounded corners
    const ImVec2 panel_min(panel_x, panel_y);
    const ImVec2 panel_max(panel_x + panel_w, panel_y + panel_h);
    dl->AddRectFilled(panel_min, panel_max, col_bg, corner_r);
    dl->PushClipRect(panel_min, panel_max, true);

    // Helper: draw centered text in a rect
    auto drawCenteredText = [&](ImFont* font, float fs, ImU32 color,
                                 float rx, float ry, float rw, float rh,
                                 const char* text)
    {
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        float tx = rx + (rw - ts.x) * 0.5f;
        float ty = ry + (rh - ts.y) * 0.5f;
        dl->AddText(font, fs, ImVec2(tx, ty), color, text);
    };

    ImFont* font_header  = m_font_title         ? m_font_title         : ImGui::GetFont(); // Russo One  — "Current Lap X"
    ImFont* font_col     = m_font_jetbrains_mono ? m_font_jetbrains_mono : ImGui::GetFont(); // JetBrains Mono — column headers (POS/DRIVER/TIME/GAP)
    ImFont* font_pos     = m_font_title         ? m_font_title         : ImGui::GetFont(); // Russo One  — position number
    ImFont* font_data    = m_font_oswald_bold   ? m_font_oswald_bold   : ImGui::GetFont(); // Oswald Bold — driver name & gap

    // =========================================================
    // HEADER: "Current Lap X"
    // =========================================================
    {
        float hy = panel_y;
        dl->AddRectFilled(ImVec2(panel_x, hy), ImVec2(panel_x + panel_w, hy + header_row_h), col_header_bg);

        int leader_lap = standings[0].currentLapNumber;
        char buf[64];
        snprintf(buf, sizeof(buf), "Current Lap %d", leader_lap);
        drawCenteredText(font_header, fs_header, col_text,
                         panel_x, hy, panel_w, header_row_h, buf);
    }

    // =========================================================
    // COLUMN HEADERS: POS | DRIVER | TIME/GAP
    // =========================================================
    {
        float chy = panel_y + header_row_h;
        dl->AddRectFilled(ImVec2(panel_x, chy), ImVec2(panel_x + panel_w, chy + col_header_h), col_lap_accent);

        // POS
        drawCenteredText(font_col, fs_col, col_text,
                         panel_x, chy, col_pos, col_header_h, "POS");
        // Divider after POS
        float div1x = panel_x + col_pos;
        dl->AddLine(ImVec2(div1x, chy), ImVec2(div1x, chy + col_header_h), col_divider, 1.0f * ui_scale);

        // DRIVER
        drawCenteredText(font_col, fs_col, col_text,
                         div1x, chy, col_driver, col_header_h, "DRIVER");
        // Divider after DRIVER
        float div2x = div1x + col_driver;
        dl->AddLine(ImVec2(div2x, chy), ImVec2(div2x, chy + col_header_h), col_divider, 1.0f * ui_scale);

        // TIME/GAP
        drawCenteredText(font_col, fs_col, col_text,
                         div2x, chy, col_gap, col_header_h, "TIME/GAP");

        // Bottom separator line
        float bot = chy + col_header_h;
        dl->AddLine(ImVec2(panel_x, bot), ImVec2(panel_x + panel_w, bot), col_divider, 1.0f * ui_scale);
    }

    // =========================================================
    // DRIVER ROWS
    // =========================================================
    int leader_laps = standings[0].completedLaps;

    for (size_t i = 0; i < standings.size(); ++i)
    {
        const VehicleStanding& s = standings[i];
        bool is_focused = (s.vehicleID == g_focused_vehicle_id);
        bool is_leader  = (i == 0);

        float ry = panel_y + header_row_h + col_header_h + static_cast<float>(i) * row_h;

        // Row background
        ImU32 row_bg = is_focused ? col_focus_bg : col_bg;
        dl->AddRectFilled(ImVec2(panel_x, ry), ImVec2(panel_x + panel_w, ry + row_h), row_bg);

        // Yellow left accent for focused row
        if (is_focused)
            dl->AddRectFilled(ImVec2(panel_x, ry), ImVec2(panel_x + 4.0f * ui_scale, ry + row_h), col_gold);

        // (no per-row horizontal/vertical dividers)
        float div1x = panel_x + col_pos;
        float div2x = div1x + col_driver;

        // --- POS ---
        {
            char pos_buf[8];
            snprintf(pos_buf, sizeof(pos_buf), "%d", s.position);
            ImU32 pos_col = is_focused ? col_gold : col_text;
            drawCenteredText(font_pos, fs_data, pos_col,
                             panel_x, ry, col_pos, row_h, pos_buf);
        }

        // --- DRIVER: color bar + name ---
        {
            // Get vehicle color from standings vehicleID
            extern std::map<int32_t, Vehicle> g_vehicles;
            extern std::mutex g_vehicles_mutex;

            glm::vec3 veh_color(0.5f, 0.5f, 0.5f);
            std::string driver_name = "???";
            {
                std::lock_guard<std::mutex> lk(g_vehicles_mutex);
                auto it = g_vehicles.find(s.vehicleID);
                if (it != g_vehicles.end())
                {
                    veh_color = it->second.getColor();
                    // First 3 chars of name (or "CAR N")
                    std::string full = it->second.name;
                    if (full == "Unknown" || full.empty())
                    {
                        char nb[8];
                        snprintf(nb, sizeof(nb), "C%d", s.vehicleID);
                        full = nb;
                    }
                    // Take first 4 chars uppercase
                    driver_name = full.substr(0, 4);
                    for (char& c : driver_name) c = static_cast<char>(toupper(c));
                }
            }

            ImU32 bar_col = IM_COL32(
                static_cast<int>(veh_color.r * 255),
                static_cast<int>(veh_color.g * 255),
                static_cast<int>(veh_color.b * 255),
                255);

            // Color bar: wider, centered vertically, 65% row height
            float bar_w   = 5.0f * ui_scale;
            float bar_h   = row_h * 0.65f;
            float bar_x   = div1x + 5.0f * ui_scale;
            float bar_y   = ry + (row_h - bar_h) * 0.5f;
            dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), bar_col);

            // Driver name centered in remaining space between bar and div2x
            ImU32 name_col = is_focused ? col_gold : col_text;
            ImVec2 name_ts = font_data->CalcTextSizeA(fs_data, FLT_MAX, 0.0f, driver_name.c_str());
            float text_zone_x = bar_x + bar_w + 3.0f * ui_scale;
            float text_zone_w = div2x - text_zone_x;
            float name_x = text_zone_x + (text_zone_w - name_ts.x) * 0.5f;
            float name_y = ry + (row_h - name_ts.y) * 0.5f;
            dl->AddText(font_data, fs_data, ImVec2(name_x, name_y), name_col, driver_name.c_str());
        }

        // --- TIME/GAP ---
        {
            int lap_diff = leader_laps - s.completedLaps;
            char gap_buf[32];
            ImU32 gap_col = is_focused ? col_gold : col_text;

            if (is_leader)
            {
                snprintf(gap_buf, sizeof(gap_buf), "Leader");
                gap_col = col_gold;
            }
            else if (lap_diff >= 1)
            {
                if (lap_diff == 1)
                    snprintf(gap_buf, sizeof(gap_buf), "+1 LAP");
                else
                    snprintf(gap_buf, sizeof(gap_buf), "+%d LAPS", lap_diff);
                gap_col = col_lapped;
            }
            else
            {
                float delta = s.deltaTimeToLeader;
                if (delta != 0.0f)
                    snprintf(gap_buf, sizeof(gap_buf), "+%.3f", delta);
                else
                    snprintf(gap_buf, sizeof(gap_buf), "---");
            }

            drawCenteredText(font_data, fs_data, gap_col,
                             div2x, ry, col_gap, row_h, gap_buf);
        }
    }

    dl->PopClipRect();
    // Rounded outline border
    dl->AddRect(panel_min, panel_max, col_divider, corner_r, 0, 1.0f * ui_scale);

    ImGui::End();
    ImGui::PopStyleVar(3);
}
