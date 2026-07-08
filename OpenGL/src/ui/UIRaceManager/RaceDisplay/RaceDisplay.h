#pragma once

#include "RaceStatusBar.h"

class RaceDisplay
{
public:
    RaceDisplay();
    ~RaceDisplay();

    // Initialize display with screen dimensions
    void Initialize(uint32_t screenWidth, uint32_t screenHeight);

    // Handle screen resize events
    void OnScreenResized(uint32_t screenWidth, uint32_t screenHeight);

    // Update and render the display
    void Update(const ModeManager& modeManager);
    void Render(const ModeManager& modeManager);

    // Access the status bar directly if needed
    RaceStatusBar& GetStatusBar() { return m_statusBar; }
    const RaceStatusBar& GetStatusBar() const { return m_statusBar; }

private:
    RaceStatusBar m_statusBar;
    uint32_t m_screenWidth;
    uint32_t m_screenHeight;
};
