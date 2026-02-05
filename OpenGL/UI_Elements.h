#pragma once
#include <glm/glm.hpp>

// Forward declarations
struct ImFont;
class MapOrigin;

// ============================================================================
// UI Elements - Non-interactive visual elements
// Compass, Laptimer, Leaderboard, etc.
// ============================================================================

class UIElements
{
public:
    UIElements();
    ~UIElements();
    
    bool initialize();
    void shutdown();
    
    // Compass
    void drawCompass(float camera_yaw, const MapOrigin& origin);
    
    // Laptimer (with default parameters for empty state)
    void drawLapTimer(float current_lap_time = 0.0f, 
                     float last_lap_time = -1.0f,   // -1 means no data
                     float best_lap_time = -1.0f,   // -1 means no data
                     float time_diff = 0.0f);
    
    // Setters
    void setFontTitle(ImFont* font) { m_font_title = font; }
    void setCompassTexture(void* texture) { m_compass_texture = texture; }
    
private:
    // Fonts
    ImFont* m_font_title;  // Russo One Regular
    
    // Textures
    void* m_compass_texture;
    
    // Helper methods
    void drawSeparatorLine(float center_x, float y_position, 
                          float line_width, float display_height);
};
