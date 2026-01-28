#pragma once

// ============================================================================
// UI CONFIGURATION
// All user interface related constants and settings
// ============================================================================

namespace UIConfig {

// ============================================================================
// APPLICATION INFO
// ============================================================================
static constexpr const char* APP_VERSION = "v0.6.0";
static constexpr const char* APP_NAME = "R.A.J.A Prime";

// ============================================================================
// FONT SETTINGS
// ============================================================================
static constexpr float FONT_SIZE_REGULAR = 16.0f;    // Main UI font
static constexpr float FONT_SIZE_TITLE = 24.0f;      // Titles and headers
static constexpr float FONT_SIZE_RACE = 24.0f;       // F1 font for special elements

// Font paths (relative to executable)
static constexpr const char* FONT_PATH_UBUNTU_REGULAR = "styles/fonts/Ubuntu/Ubuntu-Regular.ttf";
static constexpr const char* FONT_PATH_UBUNTU_BOLD = "styles/fonts/Ubuntu/Ubuntu-Bold.ttf";
static constexpr const char* FONT_PATH_F1 = "styles/fonts/F1-Font-Family/Formula1-Regular-1.ttf";

// ============================================================================
// MENU BAR SETTINGS
// ============================================================================
static constexpr float TOP_MENU_HEIGHT = 24.0f;      // Top menu bar height (like Blender)
static constexpr float BOTTOM_MENU_HEIGHT = 22.0f;   // Bottom status bar height

// Menu spacing
static constexpr float MENU_ITEM_SPACING = 4.0f;     // Horizontal space between menu items
static constexpr float MENU_FRAME_PADDING_X = 10.0f;
static constexpr float MENU_FRAME_PADDING_Y = 4.0f;

// ============================================================================
// COLORS (HEX to RGB conversion)
// ============================================================================
// Menu background #181818
static constexpr float MENU_BG_R = 0x18 / 255.0f;
static constexpr float MENU_BG_G = 0x18 / 255.0f;
static constexpr float MENU_BG_B = 0x18 / 255.0f;

// Menu text #C8C8C8 (default gray)
static constexpr float MENU_TEXT_R = 0xC8 / 255.0f;
static constexpr float MENU_TEXT_G = 0xC8 / 255.0f;
static constexpr float MENU_TEXT_B = 0xC8 / 255.0f;

// Hover color #565656
static constexpr float HOVER_COLOR_R = 0x56 / 255.0f;
static constexpr float HOVER_COLOR_G = 0x56 / 255.0f;
static constexpr float HOVER_COLOR_B = 0x56 / 255.0f;

// Popup background #1E1E1E
static constexpr float POPUP_BG_R = 0x1E / 255.0f;
static constexpr float POPUP_BG_G = 0x1E / 255.0f;
static constexpr float POPUP_BG_B = 0x1E / 255.0f;
static constexpr float POPUP_BG_ALPHA = 0.98f;

// ============================================================================
// MODAL WINDOWS
// ============================================================================
// Help modal
static constexpr float HELP_MODAL_WIDTH = 700.0f;
static constexpr float HELP_MODAL_HEIGHT = 500.0f;

// Modal background overlay
static constexpr float MODAL_OVERLAY_ALPHA = 0.8f;   // 80% dark overlay

// Modal window styling
static constexpr float MODAL_WINDOW_PADDING_X = 20.0f;
static constexpr float MODAL_WINDOW_PADDING_Y = 15.0f;
static constexpr float MODAL_ITEM_SPACING_X = 12.0f;
static constexpr float MODAL_ITEM_SPACING_Y = 8.0f;

// Modal colors
static constexpr float MODAL_BG_R = 0.11f;
static constexpr float MODAL_BG_G = 0.11f;
static constexpr float MODAL_BG_B = 0.11f;
static constexpr float MODAL_BG_ALPHA = 0.98f;

static constexpr float MODAL_TITLE_BG_R = 0.08f;
static constexpr float MODAL_TITLE_BG_G = 0.08f;
static constexpr float MODAL_TITLE_BG_B = 0.08f;

// Modal button colors
static constexpr float MODAL_BUTTON_R = 0.25f;
static constexpr float MODAL_BUTTON_G = 0.45f;
static constexpr float MODAL_BUTTON_B = 0.75f;
static constexpr float MODAL_BUTTON_ALPHA = 0.8f;

static constexpr float MODAL_BUTTON_HOVER_R = 0.35f;
static constexpr float MODAL_BUTTON_HOVER_G = 0.55f;
static constexpr float MODAL_BUTTON_HOVER_B = 0.85f;

static constexpr float MODAL_BUTTON_ACTIVE_R = 0.45f;
static constexpr float MODAL_BUTTON_ACTIVE_G = 0.65f;
static constexpr float MODAL_BUTTON_ACTIVE_B = 0.95f;

// Modal close button
static constexpr float MODAL_CLOSE_BUTTON_WIDTH = 120.0f;
static constexpr float MODAL_CLOSE_BUTTON_HEIGHT = 30.0f;

// Modal help window columns
static constexpr float HELP_MODAL_COLUMN_WIDTH = 250.0f;

// ============================================================================
// SPLASH SCREEN
// ============================================================================
static constexpr float SPLASH_WINDOW_WIDTH_PERCENT = 0.44f;  // 44% of screen width
static constexpr float SPLASH_WINDOW_HEIGHT_PERCENT = 0.69f; // 69% of screen height

// ============================================================================
// IMGUI GLOBAL STYLE
// ============================================================================
// Rounding (set to 0 for flat Blender-like style)
static constexpr float WINDOW_ROUNDING = 0.0f;
static constexpr float CHILD_ROUNDING = 0.0f;
static constexpr float FRAME_ROUNDING = 0.0f;
static constexpr float POPUP_ROUNDING = 0.0f;
static constexpr float SCROLLBAR_ROUNDING = 0.0f;
static constexpr float GRAB_ROUNDING = 0.0f;
static constexpr float TAB_ROUNDING = 0.0f;

// Padding
static constexpr float GLOBAL_WINDOW_PADDING_X = 8.0f;
static constexpr float GLOBAL_WINDOW_PADDING_Y = 8.0f;
static constexpr float GLOBAL_FRAME_PADDING_X = 8.0f;
static constexpr float GLOBAL_FRAME_PADDING_Y = 4.0f;
static constexpr float GLOBAL_ITEM_SPACING_X = 8.0f;
static constexpr float GLOBAL_ITEM_SPACING_Y = 4.0f;
static constexpr float GLOBAL_ITEM_INNER_SPACING_X = 6.0f;
static constexpr float GLOBAL_ITEM_INNER_SPACING_Y = 4.0f;
static constexpr float GLOBAL_INDENT_SPACING = 20.0f;

// Borders
static constexpr float WINDOW_BORDER_SIZE = 0.0f;
static constexpr float CHILD_BORDER_SIZE = 0.0f;
static constexpr float POPUP_BORDER_SIZE = 1.0f;
static constexpr float FRAME_BORDER_SIZE = 0.0f;

} // namespace UIConfig
