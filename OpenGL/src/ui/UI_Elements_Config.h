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
    
    // ========== LEADERBOARD ==========
    namespace Leaderboard
    {
        // Size based on design: 185x459 at 1600x900 window
        static constexpr float WIDTH_RATIO = 185.0f / 1600.0f;   // 0.115625 (11.56%)
        static constexpr float HEIGHT_RATIO = 459.0f / 900.0f;   // 0.51 (51%)
        
        // Position: 26px from left at 1600x900
        static constexpr float LEFT_MARGIN_RATIO = 26.0f / 1600.0f;  // 0.01625 (1.625%)
        
        // Text padding inside: 21px from left edge
        static constexpr float TEXT_PADDING_RATIO = 21.0f / 1600.0f;  // 0.013125 (1.3%)
        
        // Header height (CURRENT LAP + separator)
        static constexpr float HEADER_HEIGHT_RATIO = 75.0f / 900.0f;  // Estimated
        
        // Row height for each driver
        static constexpr float ROW_HEIGHT_RATIO = 28.0f / 900.0f;
        
        // Separator line thickness
        static constexpr float LINE_THICKNESS = 2.0f;
        
        // Font sizes at 1600x900
        static constexpr float HEADER_SIZE_RATIO = 18.0f / 900.0f;    // "CURRENT LAP X"
        static constexpr float POSITION_SIZE_RATIO = 20.0f / 900.0f;  // Position number
        static constexpr float NAME_SIZE_RATIO = 18.0f / 900.0f;      // Driver name
        static constexpr float DELTA_SIZE_RATIO = 18.0f / 900.0f;     // Time delta
        
        // Colors (RGB hex format)
        // Background: 181818
        static constexpr float BG_COLOR_R = 0x18 / 255.0f;
        static constexpr float BG_COLOR_G = 0x18 / 255.0f;
        static constexpr float BG_COLOR_B = 0x18 / 255.0f;
        static constexpr float BG_COLOR_A = 1.0f;
        
        // Leader color: DAA540
        static constexpr float LEADER_COLOR_R = 0xDA / 255.0f;
        static constexpr float LEADER_COLOR_G = 0xA5 / 255.0f;
        static constexpr float LEADER_COLOR_B = 0x40 / 255.0f;
        
        // Position/Name/Time color: D5D5D5
        static constexpr float TEXT_COLOR_R = 0xD5 / 255.0f;
        static constexpr float TEXT_COLOR_G = 0xD5 / 255.0f;
        static constexpr float TEXT_COLOR_B = 0xD5 / 255.0f;
        
        // Lap number color: F0F0F0
        static constexpr float LAP_COLOR_R = 0xF0 / 255.0f;
        static constexpr float LAP_COLOR_G = 0xF0 / 255.0f;
        static constexpr float LAP_COLOR_B = 0xF0 / 255.0f;
        
        // Lapped drivers color: F3CE87
        static constexpr float LAPPED_COLOR_R = 0xF3 / 255.0f;
        static constexpr float LAPPED_COLOR_G = 0xCE / 255.0f;
        static constexpr float LAPPED_COLOR_B = 0x87 / 255.0f;
        
        // Focused vehicle background: B08023
        static constexpr float FOCUS_BG_R = 0xB0 / 255.0f;
        static constexpr float FOCUS_BG_G = 0x80 / 255.0f;
        static constexpr float FOCUS_BG_B = 0x23 / 255.0f;
        static constexpr float FOCUS_BG_A = 1.0f;
        
        // Separator line color: Same as leader
        static constexpr float LINE_COLOR_R = LEADER_COLOR_R;
        static constexpr float LINE_COLOR_G = LEADER_COLOR_G;
        static constexpr float LINE_COLOR_B = LEADER_COLOR_B;
    }
}
