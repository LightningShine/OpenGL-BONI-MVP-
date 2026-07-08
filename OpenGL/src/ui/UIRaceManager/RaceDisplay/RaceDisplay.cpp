#include "RaceDisplay.h"

RaceDisplay::RaceDisplay()
    : m_screenWidth(1600)
    , m_screenHeight(900)
{
}

RaceDisplay::~RaceDisplay()
{
}

void RaceDisplay::Initialize(uint32_t screenWidth, uint32_t screenHeight)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_statusBar.Initialize(screenWidth, screenHeight);
}

void RaceDisplay::OnScreenResized(uint32_t screenWidth, uint32_t screenHeight)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_statusBar.OnScreenResized(screenWidth, screenHeight);
}

void RaceDisplay::Update(const ModeManager& modeManager)
{
    // Update logic can be added here if needed in the future
}

void RaceDisplay::Render(const ModeManager& modeManager)
{
    m_statusBar.Render(modeManager);
}
