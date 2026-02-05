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
    
    // Laptimer
    void drawLapTimer(float current_lap_time, float last_lap_time, 
                     float best_lap_time, float time_diff);
    
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
