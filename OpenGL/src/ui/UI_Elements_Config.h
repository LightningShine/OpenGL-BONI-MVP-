#pragma once

// ============================================================================
// UI Elements Configuration
// Non-interactive elements: Compass, Laptimer, Leaderboard
// ============================================================================

namespace UIElementsConfig
{
    // ========== COMPASS ==========
    namespace Compass
    {
        // Size based on design: 267x246 at 1600x900 window
        static constexpr float WIDTH_RATIO = 267.0f / 1600.0f;   // 0.166875 (16.69%)
        static constexpr float HEIGHT_RATIO = 246.0f / 900.0f;   // 0.273333 (27.33%)
        
        // Spacing: 80px at 1600x900 = 0.05 (5% of width)
        static constexpr float SPACING_RATIO = 80.0f / 1600.0f;  // 0.05 (5%)
        
        // Triangle size: 11.04x10.81 at 1600x900
        static constexpr float TRIANGLE_WIDTH_RATIO = 11.04f / 1600.0f;
        static constexpr float TRIANGLE_HEIGHT_RATIO = 10.81f / 900.0f;
        
        // Arrow position: -20px from top at 1600x900
        static constexpr float ARROW_OFFSET_RATIO = 20.0f / 900.0f;  // 0.022222
        
        // Triangle color (golden like separator lines - DAA940)
        static constexpr unsigned char TRIANGLE_COLOR_R = 0xDA;
        static constexpr unsigned char TRIANGLE_COLOR_G = 0xA9;
        static constexpr unsigned char TRIANGLE_COLOR_B = 0x40;
        static constexpr unsigned char TRIANGLE_COLOR_A = 255;
    }
    
    // ========== LAPTIMER ==========
    namespace LapTimer
    {
        // Size based on design: 259x217 at 1600x900 window
        static constexpr float WIDTH_RATIO = 259.0f / 1600.0f;   // 0.161875 (16.19%)
        static constexpr float HEIGHT_RATIO = 217.0f / 900.0f;   // 0.241111 (24.11%)
        // Separator line width: 176px at 1600x900
        static constexpr float LINE_WIDTH_RATIO = 176.0f / 1600.0f;  // 0.11 (11%)
        static constexpr float LINE_THICKNESS = 2.0f;
        
        // Spacing and padding
        static constexpr float LEFT_PADDING_RATIO = 40.0f / 1600.0f;        // 0.025 (2.5%)
        static constexpr float TOP_SPACING_RATIO = 28.0f / 900.0f;          // 0.031111 (3.11%)
        static constexpr float ELEMENT_SPACING_RATIO = 10.0f / 900.0f;      // 0.011111 (1.11%)
        
        // Font sizes at 1600x900 (increased to match reference design)
        static constexpr float TITLE_SIZE_RATIO = 18.0f / 900.0f;      // 0.022222 (LAPTIME text)
        static constexpr float MAIN_TIME_SIZE_RATIO = 44.0f / 900.0f;  // 0.077778 (Main time - HUGE!)
        static constexpr float LABEL_SIZE_RATIO = 16.0f / 900.0f;      // 0.017778 (LAST LAP, BEST LAP)
        static constexpr float TIME_SIZE_RATIO = 22.0f / 900.0f;       // 0.033333 (Time digits)
        
        // Colors (RGB hex format)
        // Title and labels color: A4A3A3
        static constexpr float LABEL_COLOR_R = 0xA4 / 255.0f;
        static constexpr float LABEL_COLOR_G = 0xA3 / 255.0f;
        static constexpr float LABEL_COLOR_B = 0xA3 / 255.0f;
        
        // Time digits color: F0F0F0
        static constexpr float TIME_COLOR_R = 0xF0 / 255.0f;
        static constexpr float TIME_COLOR_G = 0xF0 / 255.0f;
        static constexpr float TIME_COLOR_B = 0xF0 / 255.0f;
        
        // Separator line color: DAA940
        static constexpr float LINE_COLOR_R = 0xDA / 255.0f;
        static constexpr float LINE_COLOR_G = 0xA9 / 255.0f;
        static constexpr float LINE_COLOR_B = 0x40 / 255.0f;
    }
}
