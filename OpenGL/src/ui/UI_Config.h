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
static constexpr float MENU_ITEM_SPACING = 0.0f;     // Horizontal space between menu items
static constexpr float MENU_FRAME_PADDING_X = 10.0f;
static constexpr float MENU_FRAME_PADDING_Y = 4.0f;
static constexpr float MENU_LEFT_PADDING = 16.0f;     // Отступ первого элемента меню от левого края

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

// ============================================================================
// MODAL WINDOW SETTINGS (Help Modal, etc.)
// ============================================================================
// Modal window padding
static constexpr float MODAL_PADDING_X = 20.0f;          // Horizontal padding
static constexpr float MODAL_PADDING_Y = 15.0f;          // Vertical padding

// Modal item spacing
static constexpr float MODAL_ITEM_SPACING_X = 12.0f;     // Horizontal spacing
static constexpr float MODAL_ITEM_SPACING_Y = 12.0f;      // Vertical spacing

// Modal colors - Background
static constexpr float MODAL_BG_R = 0x1C / 255.0f;       // Background #1C1C1C
static constexpr float MODAL_BG_G = 0x1C / 255.0f;
static constexpr float MODAL_BG_B = 0x1C / 255.0f;
static constexpr float MODAL_BG_ALPHA = 0.98f;           // 98% opacity

// Modal colors - Text
static constexpr float MODAL_TEXT_R = 1.0f;              // White text
static constexpr float MODAL_TEXT_G = 1.0f;
static constexpr float MODAL_TEXT_B = 1.0f;

// Modal colors - Title bar
static constexpr float MODAL_TITLE_BG_R = 0x14 / 255.0f;     // Title background #141414
static constexpr float MODAL_TITLE_BG_G = 0x14 / 255.0f;
static constexpr float MODAL_TITLE_BG_B = 0x14 / 255.0f;

static constexpr float MODAL_TITLE_ACTIVE_R = 0x1E / 255.0f; // Active title #1E1E1E
static constexpr float MODAL_TITLE_ACTIVE_G = 0x1E / 255.0f;
static constexpr float MODAL_TITLE_ACTIVE_B = 0x1E / 255.0f;

// Modal colors - Close Button
static constexpr float MODAL_BUTTON_R = 0xB8 / 255.0f;      // Button #B80000 (red)
static constexpr float MODAL_BUTTON_G = 0x00 / 255.0f;
static constexpr float MODAL_BUTTON_B = 0x00 / 255.0f;
static constexpr float MODAL_BUTTON_ALPHA = 1.0f;           // 100% opacity

static constexpr float MODAL_BUTTON_HOVER_R = 0x79 / 255.0f;  // Hover #790000
static constexpr float MODAL_BUTTON_HOVER_G = 0x00 / 255.0f;
static constexpr float MODAL_BUTTON_HOVER_B = 0x00 / 255.0f;

static constexpr float MODAL_BUTTON_ACTIVE_R = 0xB8 / 255.0f; // Active #B80000
static constexpr float MODAL_BUTTON_ACTIVE_G = 0x00 / 255.0f;
static constexpr float MODAL_BUTTON_ACTIVE_B = 0x00 / 255.0f;

// Modal button size
static constexpr float MODAL_BUTTON_WIDTH = 120.0f;
static constexpr float MODAL_BUTTON_HEIGHT = 30.0f;

// ============================================================================
// MODAL TITLE BAR SETTINGS
// ============================================================================
// Title bar padding
static constexpr float MODAL_TITLE_PADDING_X = 10.0f;        // Horizontal padding
static constexpr float MODAL_TITLE_PADDING_Y = 8.0f;         // Vertical padding

// Close button (X) settings
static constexpr float MODAL_CLOSE_BUTTON_SIZE = 20.0f;      // Size of X button
static constexpr float MODAL_CLOSE_BUTTON_ROUNDING = 0.0f;   // Corner rounding (0 = flat)

// Close button colors
static constexpr float MODAL_CLOSE_HOVER_R = 0xE8 / 255.0f;  // Hover color #E81123 (red)
static constexpr float MODAL_CLOSE_HOVER_G = 0x11 / 255.0f;
static constexpr float MODAL_CLOSE_HOVER_B = 0x23 / 255.0f;

static constexpr float MODAL_CLOSE_ACTIVE_R = 0xC4 / 255.0f; // Active color #C42B1C
static constexpr float MODAL_CLOSE_ACTIVE_G = 0x2B / 255.0f;
static constexpr float MODAL_CLOSE_ACTIVE_B = 0x1C / 255.0f;

// ============================================================================
// DROPDOWN MENU SETTINGS
// ============================================================================
// Dropdown menu size
static constexpr float DROPDOWN_MIN_WIDTH = 600.0f;       // Minimum width of dropdown
static constexpr float DROPDOWN_PADDING_X = 30.0f;        // Horizontal padding inside menu
static constexpr float DROPDOWN_PADDING_Y = 8.0f;         // Vertical padding inside menu

// Menu item spacing
static constexpr float DROPDOWN_ITEM_SPACING_X = 10.0f;    // Horizontal spacing between elements
static constexpr float DROPDOWN_ITEM_SPACING_Y = 4.0f;    // Vertical spacing between items
static constexpr float DROPDOWN_ITEM_INNER_SPACING = 10.0f; // Space between text and shortcut

// Menu item padding (inside each menu item)
static constexpr float DROPDOWN_ITEM_PADDING_X = 10.0f;   // Horizontal padding in menu item
static constexpr float DROPDOWN_ITEM_PADDING_Y = 6.0f;    // Vertical padding in menu item

// Dropdown colors
static constexpr float DROPDOWN_BG_R = 0x1E / 255.0f;     // Background #1E1E1E
static constexpr float DROPDOWN_BG_G = 0x1E / 255.0f;
static constexpr float DROPDOWN_BG_B = 0x1E / 255.0f;
static constexpr float DROPDOWN_BG_ALPHA = 1.0f;         // 100	% opacity

// Menu item text color
static constexpr float DROPDOWN_TEXT_R = 0xE8 / 255.0f;   // Text color #E8E8E8
static constexpr float DROPDOWN_TEXT_G = 0xE8 / 255.0f;
static constexpr float DROPDOWN_TEXT_B = 0xE8 / 255.0f;

// Menu item hover color
static constexpr float DROPDOWN_HOVER_R = 0x3A / 255.0f;  // Hover color #3A3A3A
static constexpr float DROPDOWN_HOVER_G = 0x3A / 255.0f;
static constexpr float DROPDOWN_HOVER_B = 0x3A / 255.0f;

// Menu item active/selected color
static constexpr float DROPDOWN_ACTIVE_R = 0x45 / 255.0f; // Active color #454545
static constexpr float DROPDOWN_ACTIVE_G = 0x45 / 255.0f;
static constexpr float DROPDOWN_ACTIVE_B = 0x45 / 255.0f;

// Separator color
static constexpr float DROPDOWN_SEPARATOR_R = 0x40 / 255.0f; // Separator #404040
static constexpr float DROPDOWN_SEPARATOR_G = 0x40 / 255.0f;
static constexpr float DROPDOWN_SEPARATOR_B = 0x40 / 255.0f;

// Dropdown border
static constexpr float DROPDOWN_BORDER_R = 0x2B / 255.0f; // Border color #2B2B2B
static constexpr float DROPDOWN_BORDER_G = 0x2B / 255.0f;
static constexpr float DROPDOWN_BORDER_B = 0x2B / 255.0f;
static constexpr float DROPDOWN_BORDER_SIZE = 2.0f;       // Border thickness

// Dropdown rounding
static constexpr float DROPDOWN_ROUNDING = 2.0f;          // Corner rounding (0 = flat)

} // namespace UIConfig
