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
        
        // Triangle color (golden yellow)
        static constexpr unsigned char TRIANGLE_COLOR_R = 255;
        static constexpr unsigned char TRIANGLE_COLOR_G = 215;
        static constexpr unsigned char TRIANGLE_COLOR_B = 0;
        static constexpr unsigned char TRIANGLE_COLOR_A = 255;
    }
    
    // ========== LAPTIMER ==========
    namespace LapTimer
    {
        // Size based on design: 259x217 at 1600x900 window
        static constexpr float WIDTH_RATIO = 259.0f / 1600.0f;   // 0.161875 (16.19%)
        static constexpr float HEIGHT_RATIO = 217.0f / 900.0f;   // 0.241111 (24.11%)
        
        // Separator line width: 172px at 1600x900
        static constexpr float LINE_WIDTH_RATIO = 172.0f / 1600.0f;  // 0.1075 (10.75%)
        static constexpr float LINE_THICKNESS = 2.0f;
        
        // Spacing and padding
        static constexpr float LEFT_PADDING_RATIO = 40.0f / 1600.0f;        // 0.025 (2.5%)
        static constexpr float TOP_SPACING_RATIO = 28.0f / 900.0f;          // 0.031111 (3.11%)
        static constexpr float ELEMENT_SPACING_RATIO = 10.0f / 900.0f;      // 0.011111 (1.11%)
        
        // Font sizes at 1600x900
        static constexpr float TITLE_SIZE_RATIO = 16.0f / 900.0f;      // 0.017778
        static constexpr float MAIN_TIME_SIZE_RATIO = 36.0f / 900.0f;  // 0.04
        static constexpr float LABEL_SIZE_RATIO = 12.0f / 900.0f;      // 0.013333
        static constexpr float TIME_SIZE_RATIO = 20.0f / 900.0f;       // 0.022222
        
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
