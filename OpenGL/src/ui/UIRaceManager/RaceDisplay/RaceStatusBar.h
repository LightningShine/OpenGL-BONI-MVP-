#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include "RaceFlags.h"

class ModeManager;

class RaceStatusBar
{
public:
    RaceStatusBar();
    ~RaceStatusBar();

    // Initialize with screen dimensions (for responsive sizing)
    void Initialize(uint32_t screenWidth, uint32_t screenHeight);

    // Update session time (in milliseconds)
    void UpdateSessionTime(uint32_t sessionTimeMs);

    // Render the status bar background and flags (call this during UI render pass)
    void Render(const ModeManager& modeManager);

    // Get formatted strings for ImGui rendering
    std::string GetSystemTimeString() const;
    std::string GetSessionTimeString() const;

    // Get responsive dimensions
    float GetBarWidth() const { return m_barWidth; }
    float GetBarHeight() const { return m_barHeight; }
    float GetXPosition() const { return m_posX; }
    float GetYPosition() const { return m_posY; }

    // Get flag manager
    RaceFlags& GetFlags() { return m_flags; }
    const RaceFlags& GetFlags() const { return m_flags; }

    // Update screen resolution (for responsive resizing)
    void OnScreenResized(uint32_t screenWidth, uint32_t screenHeight);

private:
    void RecalculateDimensions();
    void DrawRectangle(float x, float y, float width, float height, float r, float g, float b, float a);
    void DrawBorderedRectangle(float x, float y, float width, float height, 
                                float r, float g, float b, float a,
                                float borderR, float borderG, float borderB, float borderA,
                                float borderThickness);
    void DrawText(float x, float y, float scale, float r, float g, float b, const char* text);
    std::string FormatTime(uint32_t milliseconds) const;

    // Screen dimensions
    uint32_t m_screenWidth;
    uint32_t m_screenHeight;

    // Responsive bar dimensions
    float m_barWidth;
    float m_barHeight;
    float m_posX;
    float m_posY;

    // Calculated element sizes (responsive)
    float m_flagSize;
    float m_flagSpacing;
    float m_textScale;
    float m_borderThickness;

    // Session timing
    uint32_t m_sessionTimeMs;
    std::chrono::steady_clock::time_point m_lastUpdateTime;

    // Flag manager
    RaceFlags m_flags;

    // Base reference values for responsive scaling (1600x900 is the reference)
    static constexpr float REFERENCE_WIDTH = 1600.0f;
    static constexpr float REFERENCE_HEIGHT = 900.0f;
    static constexpr float BASE_BAR_WIDTH = 256.0f;
    static constexpr float BASE_BAR_HEIGHT = 25.0f;
    static constexpr float BASE_FLAG_SIZE = 16.0f;
    static constexpr float BASE_TEXT_SCALE = 0.5f;
};
