#include "RaceStatusBar.h"
#include "../../../racing/ModeManager/ModeManager.h"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

RaceStatusBar::RaceStatusBar()
    : m_screenWidth(1600)
    , m_screenHeight(900)
    , m_barWidth(BASE_BAR_WIDTH)
    , m_barHeight(BASE_BAR_HEIGHT)
    , m_posX(0.0f)
    , m_posY(0.0f)
    , m_flagSize(BASE_FLAG_SIZE)
    , m_flagSpacing(4.0f)
    , m_textScale(BASE_TEXT_SCALE)
    , m_borderThickness(1.5f)
    , m_sessionTimeMs(0)
{
    RecalculateDimensions();
}

RaceStatusBar::~RaceStatusBar()
{
}

void RaceStatusBar::Initialize(uint32_t screenWidth, uint32_t screenHeight)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    OnScreenResized(screenWidth, screenHeight);
}

void RaceStatusBar::OnScreenResized(uint32_t screenWidth, uint32_t screenHeight)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    RecalculateDimensions();
}

void RaceStatusBar::RecalculateDimensions()
{
    // Calculate scale factor based on screen dimensions
    float scaleX = static_cast<float>(m_screenWidth) / REFERENCE_WIDTH;
    float scaleY = static_cast<float>(m_screenHeight) / REFERENCE_HEIGHT;
    float scale = (scaleX + scaleY) * 0.5f;  // Use average scale

    // Responsive sizing
    m_barWidth = BASE_BAR_WIDTH * scaleX;
    m_barHeight = BASE_BAR_HEIGHT * scaleY;
    m_flagSize = BASE_FLAG_SIZE * scale;
    m_textScale = BASE_TEXT_SCALE * scale;
    m_borderThickness = std::max(1.0f, 1.5f * scale);
    m_flagSpacing = std::max(2.0f, 4.0f * scale);

    // Center the bar horizontally, position at top
    m_posX = (m_screenWidth - m_barWidth) * 0.5f;
    m_posY = 0.0f;
}

void RaceStatusBar::UpdateSessionTime(uint32_t sessionTimeMs)
{
    m_sessionTimeMs = sessionTimeMs;
    m_lastUpdateTime = std::chrono::steady_clock::now();
}

std::string RaceStatusBar::FormatTime(uint32_t milliseconds) const
{
    uint32_t totalSeconds = milliseconds / 1000;
    uint32_t minutes = totalSeconds / 60;
    uint32_t seconds = totalSeconds % 60;

    std::stringstream ss;
    ss << std::setfill('0') 
       << std::setw(2) << minutes << ":"
       << std::setw(2) << seconds;
    return ss.str();
}

void RaceStatusBar::Render(const ModeManager& modeManager)
{
    // Draw background bar with border
    DrawBorderedRectangle(m_posX, m_posY, m_barWidth, m_barHeight,
                          0.1f, 0.1f, 0.1f, 0.9f,  // Dark background
                          0.3f, 0.3f, 0.3f, 1.0f,  // Gray border
                          m_borderThickness);

    // Calculate layout positions
    float padding = m_flagSpacing * 2;
    float elementSpacing = m_flagSpacing;
    float contentHeight = m_barHeight - padding;
    float verticalCenter = m_posY + m_barHeight * 0.5f;

    // Left flag position
    float leftFlagX = m_posX + padding;
    float leftFlagY = verticalCenter - m_flagSize * 0.5f;

    // Right flag position
    float rightFlagX = m_posX + m_barWidth - padding - m_flagSize;

    // Draw left flag — colors come from m_flags (set from the current race
    // flag: server flag when connected, green by default).
    float flagR, flagG, flagB, flagA;
    RaceFlags::GetFlagRGBA(m_flags.GetLeftFlag(), flagR, flagG, flagB, flagA);
    DrawBorderedRectangle(leftFlagX, leftFlagY, m_flagSize, m_flagSize,
                          flagR, flagG, flagB, flagA,
                          0.5f, 0.5f, 0.5f, 1.0f,
                          1.0f);

    // Draw right flag
    RaceFlags::GetFlagRGBA(m_flags.GetRightFlag(), flagR, flagG, flagB, flagA);
    DrawBorderedRectangle(rightFlagX, leftFlagY, m_flagSize, m_flagSize,
                          flagR, flagG, flagB, flagA,
                          0.5f, 0.5f, 0.5f, 1.0f,
                          1.0f);

    // Note: Text rendering (system time, session time, phase label) should be done
    // by the UI layer using ImGui after this method returns.
    // Use GetSystemTimeString() and GetSessionTimeString() to get formatted text.
}

void RaceStatusBar::DrawRectangle(float x, float y, float width, float height, 
                                   float r, float g, float b, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);

    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();

    glEnable(GL_TEXTURE_2D);
}

void RaceStatusBar::DrawBorderedRectangle(float x, float y, float width, float height,
                                          float r, float g, float b, float a,
                                          float borderR, float borderG, float borderB, float borderA,
                                          float borderThickness)
{
    // Draw fill
    DrawRectangle(x, y, width, height, r, g, b, a);

    // Draw border (4 lines)
    glDisable(GL_TEXTURE_2D);
    glColor4f(borderR, borderG, borderB, borderA);
    glLineWidth(borderThickness);

    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();

    glLineWidth(1.0f);
    glEnable(GL_TEXTURE_2D);
}

void RaceStatusBar::DrawText(float x, float y, float scale, float r, float g, float b, const char* text)
{
    // Text rendering is handled by the UI system (ImGui)
    // This method is called from Render(), but actual text drawing should be done
    // through ImGui's draw lists in the UI layer after this rendering pass.
    // 
    // Usage in UI layer:
    // ImGui::GetForegroundDrawList()->AddText(
    //     ImVec2(x, y),
    //     ImGui::GetColorU32(ImVec4(r, g, b, 1.0f)),
    //     text
    // );
}

std::string RaceStatusBar::GetSystemTimeString() const
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &time_t);

    std::stringstream ss;
    ss << std::setfill('0')
       << std::setw(2) << timeinfo.tm_hour << ":"
       << std::setw(2) << timeinfo.tm_min;
    return ss.str();
}

std::string RaceStatusBar::GetSessionTimeString() const
{
    return FormatTime(m_sessionTimeMs);
}
