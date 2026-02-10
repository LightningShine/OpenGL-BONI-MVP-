#include "src/input/Input.h"
#include "UI.h"
#include "UI_Elements.h"
#include "src/Config.h"
#include "src/ui/UI_Config.h"
#include "src/rendering/Interpolation.h"
#include "src/rendering/Render.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <GeographicLib/UTMUPS.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "libraries/include/stb_image.h"

#include "libraries/include/imgui/imgui.h"
#include "libraries/include/imgui/backends/imgui_impl_glfw.h"
#include "libraries/include/imgui/backends/imgui_impl_opengl3.h"

// Windows API for native file dialogs (include AFTER C++ standard library)
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>  // For GetOpenFileNameA

static void AddDashedRect(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float thickness, float dash_len, float gap_len)
{
    // Top
    for (float x = p_min.x; x < p_max.x; x += dash_len + gap_len)
        draw_list->AddLine(ImVec2(x, p_min.y), ImVec2(std::min(x + dash_len, p_max.x), p_min.y), col, thickness);
    // Bottom
    for (float x = p_min.x; x < p_max.x; x += dash_len + gap_len)
        draw_list->AddLine(ImVec2(x, p_max.y), ImVec2(std::min(x + dash_len, p_max.x), p_max.y), col, thickness);
    // Left
    for (float y = p_min.y; y < p_max.y; y += dash_len + gap_len)
        draw_list->AddLine(ImVec2(p_min.x, y), ImVec2(p_min.x, std::min(y + dash_len, p_max.y)), col, thickness);
    // Right
    for (float y = p_min.y; y < p_max.y; y += dash_len + gap_len)
        draw_list->AddLine(ImVec2(p_max.x, y), ImVec2(p_max.x, std::min(y + dash_len, p_max.y)), col, thickness);
}

UI::UI()
: m_window(nullptr)
, m_context(nullptr)
, m_showSplash(true)
, m_closeSplash(false)
, m_show_help_modal(false)
, m_fontRegular(nullptr)
, m_fontUI(nullptr)
, m_fontTitle(nullptr)
, m_fontRace(nullptr)
, m_backgroundTexture(nullptr)
    , m_iconFile(nullptr)
    , m_iconContact(nullptr)
    , m_iconCopyright(nullptr)
    , m_iconHeart(nullptr)
    , m_iconClose(nullptr)
    , m_iconDragDrop(nullptr)
    , m_compassTexture(nullptr)
    , m_points(nullptr)
    , m_pointsMutex(nullptr)
{
}

UI::~UI()
{
    Shutdown();
}

bool UI::LoadTextureFromFile(const char* filename, void** out_texture, int* out_width, int* out_height)
{
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    
    if (data == nullptr)
    {
        std::cerr << "[UI] Failed to load texture: " << filename << "\n";
        return false;
    }
    
    unsigned int texture;
    glGenTextures(1, &texture);
    glBindTexture(0x0DE1, texture); // GL_TEXTURE_2D
    glTexParameteri(0x0DE1, 0x2801, 0x2601); // GL_TEXTURE_MIN_FILTER, GL_LINEAR
    glTexParameteri(0x0DE1, 0x2800, 0x2601); // GL_TEXTURE_MAG_FILTER, GL_LINEAR
    glTexParameteri(0x0DE1, 0x2802, 0x812F); // GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE
    glTexParameteri(0x0DE1, 0x2803, 0x812F); // GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE
    glTexImage2D(0x0DE1, 0, 0x1908, width, height, 0, 0x1908, 0x1401, data); // GL_RGBA, GL_UNSIGNED_BYTE
    
    stbi_image_free(data);
    
    *out_texture = (void*)(intptr_t)texture;
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    
    std::cout << "[UI] Loaded texture: " << filename << " (" << width << "x" << height << ")\n";
    return true;
}

void UI::LoadResources()
{
    int w, h;
    // ?????? ???? ? ???????????
    if (!LoadTextureFromFile("styles/images/Background.png", &m_backgroundTexture, &w, &h))
    {
        std::cerr << "[UI] Warning: Background image not loaded\n";
    }
    
    // Load Icons
    if (!LoadTextureFromFile("styles/icons/PNG/file.png", &m_iconFile, nullptr, nullptr)) std::cerr << "Failed to load file.png\n";
    if (!LoadTextureFromFile("styles/icons/PNG/contact.png", &m_iconContact, nullptr, nullptr)) std::cerr << "Failed to load contact.png\n";
    if (!LoadTextureFromFile("styles/icons/PNG/copyright.png", &m_iconCopyright, nullptr, nullptr)) std::cerr << "Failed to load copyright.png\n";
    if (!LoadTextureFromFile("styles/icons/PNG/heart.png", &m_iconHeart, nullptr, nullptr)) std::cerr << "Failed to load heart.png\n";
    if (!LoadTextureFromFile("styles/icons/PNG/circle-x.png", &m_iconClose, nullptr, nullptr)) std::cerr << "Failed to load circle-x.png\n";
    if (!LoadTextureFromFile("styles/icons/PNG/DragAndDrop.png", &m_iconDragDrop, nullptr, nullptr)) std::cerr << "Failed to load DragAndDrop.png\n";
    
    // Load Compass texture
    if (!LoadTextureFromFile("styles/images/Compas scaled.png", &m_compassTexture, nullptr, nullptr)) 
    {
        std::cerr << "[UI] Failed to load Compas scaled.png\n";
    }
    else
    {
        std::cout << "[UI] Compass texture loaded successfully\n";
    }

    // Load recent files from saves directory
    LoadRecentFiles();
}

void UI::LoadRecentFiles()
{
    namespace fs = std::filesystem;
    
    const std::string saves_path = "src/saves";
    
    // Clear existing files
    m_recentFiles.clear();
    
    // Check if directory exists
    if (!fs::exists(saves_path) || !fs::is_directory(saves_path))
    {
        std::cout << "[UI] Saves directory not found: " << saves_path << "\n";
        std::cout << "[UI] Creating saves directory...\n";
        
        try
        {
            fs::create_directories(saves_path);
            std::cout << "[UI] Saves directory created successfully\n";
        }
        catch (const std::exception& e)
        {
            std::cerr << "[UI] Failed to create saves directory: " << e.what() << "\n";
        }
        
        return;
    }
    
    std::cout << "[UI] Scanning saves directory: " << saves_path << "\n";
    
    // Scan directory for track files (.json and .txt)
    try
    {
        for (const auto& entry : fs::directory_iterator(saves_path))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                std::string extension = entry.path().extension().string();
                
                // Load .json and .txt files
                if (extension == ".json" || extension == ".txt")
                {
                    RecentFile file;
                    file.name = filename;
                    file.path = entry.path().string();
                    
                    // Convert backslashes to forward slashes for consistency
                    std::replace(file.path.begin(), file.path.end(), '\\', '/');
                    
                    m_recentFiles.push_back(file);
                    std::cout << "[UI] Found save file: " << filename << "\n";
                }
            }
        }
        
        // Sort files alphabetically
        std::sort(m_recentFiles.begin(), m_recentFiles.end(), 
                 [](const RecentFile& a, const RecentFile& b) {
                     return a.name < b.name;
                 });
        
        std::cout << "[UI] Loaded " << m_recentFiles.size() << " save file(s)\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "[UI] Error scanning saves directory: " << e.what() << "\n";
    }
}

bool UI::Initialize(GLFWwindow* window)
{
    if (!window)
    {
        std::cerr << "[UI] Error: window is null\n";
        return false;
    }
    
    m_window = window;
    
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    
    if (!m_context)
    {
        std::cerr << "[UI] Error: Failed to create ImGui context\n";
        return false;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Get window size for relative calculations
    int window_width, window_height;
    glfwGetWindowSize(window, &window_width, &window_height);
    
    // Load Fonts with oversampling for better quality
    ImFontConfig font_config;
    font_config.OversampleH = 3;
    font_config.OversampleV = 3;
    
    // Calculate font sizes based on window height
    float font_size_menu = UIConfig::MENU_TEXT_SIZE * window_height;        // 12px for menu
    float font_size_ui = UIConfig::FONT_SIZE_REGULAR * window_height;       // 16px for UI elements
    float font_size_title = 32.0f / UIConfig::BASE_HEIGHT * window_height;  // 32px base for Russo One (better quality when scaled)
    float font_size_race = UIConfig::FONT_SIZE_RACE * window_height;
    
    m_fontRegular = io.Fonts->AddFontFromFileTTF("styles/fonts/Ubuntu/Ubuntu-Regular.ttf", font_size_menu, &font_config);
    m_fontUI = io.Fonts->AddFontFromFileTTF("styles/fonts/Ubuntu/Ubuntu-Regular.ttf", font_size_ui, &font_config);
    m_fontTitle = io.Fonts->AddFontFromFileTTF("styles/fonts/Russo_One/RussoOne-Regular.ttf", font_size_title, &font_config);
    m_fontRace = io.Fonts->AddFontFromFileTTF(UIConfig::FONT_PATH_F1, font_size_race, &font_config);
    
    // Fallback to default font if Ubuntu not found
    if (!m_fontRegular) {
        std::cerr << "[UI] Warning: Ubuntu font (menu) not found, using default\n";
        m_fontRegular = io.Fonts->AddFontDefault(&font_config);
    }
    if (!m_fontUI) {
        std::cerr << "[UI] Warning: Ubuntu font (UI) not found, using default\n";
        m_fontUI = io.Fonts->AddFontDefault(&font_config);
    }
    if (!m_fontTitle) {
        std::cerr << "[UI] Warning: RussoOne Regular not found, using default\n";
        m_fontTitle = io.Fonts->AddFontDefault(&font_config);
    }
    if (!m_fontRace) {
        std::cerr << "[UI] Warning: F1 font not found, using default\n";
        m_fontRace = io.Fonts->AddFontDefault(&font_config);
    }
    
    // Setup ImGui style - Blender-like
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Enable AntiAliasing
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.AntiAliasedLinesUseTex = true;
    style.WindowRounding = 8.0f;       
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(
        UIConfig::GLOBAL_ITEM_SPACING_X * window_width, 
        UIConfig::GLOBAL_ITEM_SPACING_Y * window_height
    );
    style.ItemInnerSpacing = ImVec2(
        UIConfig::GLOBAL_ITEM_INNER_SPACING_X * window_width, 
        UIConfig::GLOBAL_ITEM_INNER_SPACING_Y * window_height
    );
    style.IndentSpacing = UIConfig::GLOBAL_INDENT_SPACING * window_width;
    
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
    {
        std::cerr << "[UI] Error: Failed to init GLFW backend\n";
        return false;
    }
    
    if (!ImGui_ImplOpenGL3_Init("#version 460"))
    {
        std::cerr << "[UI] Error: Failed to init OpenGL3 backend\n";
        return false;
    }

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor = true; // ImGui will draw the program cursor
    
    LoadResources();
    
    // Initialize UI Elements
    m_ui_elements = new UIElements();
    if (!m_ui_elements->initialize())
    {
        std::cerr << "[UI] Error: Failed to initialize UI Elements\n";
        delete m_ui_elements;
        m_ui_elements = nullptr;
        return false;
    }
    
    // Pass fonts and textures to UI Elements
    m_ui_elements->setFontTitle(m_fontTitle);
    m_ui_elements->setCompassTexture(m_compassTexture);
    
    std::cout << "[UI] Initialized successfully\n";
    return true;
}

void UI::Shutdown()
{
    if (m_context)
    {
        if (m_backgroundTexture)
        {
            unsigned int tex = (unsigned int)(intptr_t)m_backgroundTexture;
            glDeleteTextures(1, &tex);
        }
        
        if (m_compassTexture)
        {
            unsigned int tex = (unsigned int)(intptr_t)m_compassTexture;
            glDeleteTextures(1, &tex);
        }
        
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
    }
}

void UI::BeginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UI::Render()
{
    // Render splash screen if needed
    if (m_showSplash)
    {
        RenderSplashWindow();
        return; // Don't render menus during splash
    }
    
    // Always render top and bottom menus (after splash)
    RenderTopMenu();
    RenderBottomMenu();
    
    // Render help modal if open
    RenderHelpModal();
}

void UI::EndFrame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UI::SetTrackData(std::vector<glm::vec2>* points, std::mutex* mutex)
{
    m_points = points;
    m_pointsMutex = mutex;
}

void UI::RenderSplashWindow()
{
    if (!m_showSplash) return;
    
    // Handle Ctrl+V
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
    {
        const char* clipboard = glfwGetClipboardString(m_window);
        if (clipboard && m_points && m_pointsMutex)
        {
            loadTrackFromData(std::string(clipboard), *m_points, *m_pointsMutex);
            m_showSplash = false;
            m_closeSplash = true;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    

    ImVec2 windowSize(displaySize.x * 0.44f, displaySize.y * 0.69f);
    ImVec2 windowPos((displaySize.x - windowSize.x) * 0.5f, (displaySize.y - windowSize.y) * 0.5f);
    
    // Dark background overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);


    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    
    ImGui::Begin("##DarkBg", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoInputs);
    ImGui::End();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    
    // Check click outside window
    if (ImGui::IsMouseClicked(0))
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        bool clickedOutside = 
            mousePos.x < windowPos.x || 
            mousePos.x > windowPos.x + windowSize.x ||
            mousePos.y < windowPos.y || 
            mousePos.y > windowPos.y + windowSize.y;
        
        if (clickedOutside)
        {
            m_showSplash = false;
            m_closeSplash = true;
            std::cout << "[UI] Splash closed by clicking outside\n";
            return;
        }
    }
    
    RenderMainWindow();
}

void UI::RenderMainWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    

    ImVec2 windowSize(displaySize.x * 0.44f, displaySize.y * 0.69f);
    ImVec2 windowPos((displaySize.x - windowSize.x) * 0.5f, (displaySize.y - windowSize.y) * 0.5f);
    
    ImGui::SetNextWindowPos(windowPos);
    ImGui::SetNextWindowSize(windowSize);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    
    ImGui::Begin("##SplashMain", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar);
    
    // === IMAGE ===
    if (m_backgroundTexture)
    {
        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)m_backgroundTexture,
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y - 320), // Image bootom size
            ImVec2(0, 0),
            ImVec2(1, 1),
            IM_COL32(255, 255, 255, 255),
            ImGui::GetStyle().WindowRounding,
            ImDrawFlags_RoundCornersTop
        );
    }
    else
    {
        // Fallback: dark background
        ImGui::GetWindowDrawList()->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y - 320), // Black Shape bottom size
            IM_COL32(20, 20, 25, 255),
            ImGui::GetStyle().WindowRounding,
            ImDrawFlags_RoundCornersTop
        );
    }
    
    // === TITLE "RACE APP" ===
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    
    if (m_fontUI) ImGui::PushFont(m_fontUI);  // Use 16px UI font instead of huge title font
    else if (m_fontRegular) ImGui::PushFont(m_fontRegular);

    ImGui::SetCursorPos(ImVec2(35, 35));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // White color RACE APP color
    ImGui::SetWindowFontScale(2.5f);  // Reduced from 3.5f (16px * 2.5 = 40px)
    ImGui::Text("RACE");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    
    ImGui::SetCursorPos(ImVec2(35, 110));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(2.5f);  // Reduced from 3.5f
    ImGui::Text("APP");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    
    if (m_fontRace) ImGui::PopFont();
    else if (m_fontTitle) ImGui::PopFont();

    ImGui::PopStyleVar();
    
    // === VERSION ===
    if (m_fontTitle) ImGui::PushFont(m_fontTitle);
    ImGui::SetCursorPos(ImVec2(windowSize.x - 100, 35));
    ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), "0.0.6v");
    if (m_fontTitle) ImGui::PopFont();
    
    // === AUTHOR NAME ===
    if (m_fontTitle) ImGui::PushFont(m_fontTitle);
    ImGui::SetCursorPos(ImVec2(35, windowSize.y - 350));
    ImGui::TextColored(ImVec4(0.525f, 0.525f, 0.525f, 1.0f), "Uladizmir Liubamirski");
    if (m_fontTitle) ImGui::PopFont();
    
    // === CREATE TRACK BUTTON ===
    ImGui::SetCursorPos(ImVec2(18, windowSize.y - 290));
    
    if (m_fontTitle) ImGui::PushFont(m_fontTitle);
    if (ImGui::Button("Create Track", ImVec2(395, 60)))
    {
        std::cout << "[UI] Create Track clicked\n";
        m_showSplash = false;
        m_closeSplash = true;
    }
    if (m_fontTitle) ImGui::PopFont();

    // === SEPARATORS ===
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 separatorColor = IM_COL32(100, 100, 100, 255);
    
    // CHANGE HEIGHT HERE: (windowSize.y - 40) is the Y position. Increase 40 to move higher, decrease to move lower.
    float bottomBarY = windowSize.y - 45; // Moved higher to give space for content below

    // Vertical Line
    drawList->AddLine(
        ImVec2(windowPos.x + 425, windowPos.y + (windowSize.y - 290)),
        ImVec2(windowPos.x + 425, windowPos.y + bottomBarY),
        separatorColor, 2.0f // Thicker line
    );

    // Horizontal Line
    drawList->AddLine(
        ImVec2(windowPos.x, windowPos.y + bottomBarY),
        ImVec2(windowPos.x + windowSize.x, windowPos.y + bottomBarY),
        separatorColor, 2.0f // Thicker line
    );
    
    // === DRAG AND DROP ZONE ===
    ImGui::SetCursorPos(ImVec2(21, windowSize.y - 215));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    // Remove border color push as we draw it manually
    // ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.525f, 0.525f, 0.525f, 0.6f));
    // ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
    // ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    
    // Disable border in BeginChild
	// Increased height to 160 (was 140) DRAG AND DROP AREA
    ImGui::BeginChild("##DragDrop", ImVec2(390, 160), false, ImGuiWindowFlags_NoScrollbar);
    
    // Check hover
    bool isHovered = ImGui::IsWindowHovered();
    ImU32 borderColor = isHovered ? IM_COL32(0, 184, 190, 255) : separatorColor; // Cyan if hovered

    // Draw Dashed Border
    ImVec2 ddMin = ImVec2(windowPos.x + 21, windowPos.y + (windowSize.y - 215));
    ImVec2 ddMax = ImVec2(ddMin.x + 390, ddMin.y + 160); // Increased height to 160
    AddDashedRect(drawList, ddMin, ddMax, borderColor, 2.0f, 10.0f, 5.0f); // Thicker line (2.0f)

    // Draw Download Icon
    if (m_iconDragDrop)
    {
        ImGui::SetCursorPos(ImVec2(179, 55)); // Moved down (was 40)
        ImGui::Image((ImTextureID)m_iconDragDrop, ImVec2(32, 32));
    }

    ImGui::SetCursorPos(ImVec2(65, 110)); // Adjusted position
    ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), "Drag and Drop or paste from clipboard");
    
    // Removed Button

    ImGui::EndChild();
    
    ImGui::PopStyleColor(1); // Only ChildBg was pushed
    // ImGui::PopStyleVar(2); // Removed vars
    
    // === RECENT FILES ===
    // Position at right side of window (after vertical separator)
    float files_x_pos = 435;  // This is relative to window, not screen
    
    ImGui::SetCursorPos(ImVec2(files_x_pos, windowSize.y - 290));
    ImGui::TextColored(ImVec4(0.525f, 0.525f, 0.525f, 1.0f), "Recent Files");
    
    // Recent files list
    float listStartY = windowSize.y - 255;
    for (size_t i = 0; i < m_recentFiles.size() && i < 6; i++)
    {
        ImGui::SetCursorPos(ImVec2(files_x_pos, listStartY + i * 30));
        
        // Draw File Icon
        if (m_iconFile)
        {
            ImGui::Image((ImTextureID)m_iconFile, ImVec2(18, 18));
        }
        
        ImGui::SameLine();
        ImGui::SetCursorPosX(files_x_pos + 30);  // Icon width + small gap
        ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), m_recentFiles[i].name.c_str());
        
        // Clickable area
        ImGui::SetCursorPos(ImVec2(files_x_pos, listStartY + i * 30));
        if (ImGui::InvisibleButton(("##file" + std::to_string(i)).c_str(), ImVec2(390, 28)))
        {
            std::cout << "[UI] Opening: " << m_recentFiles[i].path << "\n";
            
            // Load file
            std::ifstream file(m_recentFiles[i].path);
            if (file.is_open() && m_points && m_pointsMutex)
            {
                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();
                
                loadTrackFromData(buffer.str(), *m_points, *m_pointsMutex);
                std::cout << "[UI] Track loaded from: " << m_recentFiles[i].path << "\n";
                
                // Recenter track to (0, 0) if closed
                {
                    std::lock_guard<std::mutex> lock(*m_pointsMutex);
                    TrackCenterInfo center_info = calculateTrackCenter(*m_points);
                    
                    if (center_info.is_closed)
                    {
                        std::cout << "[TRACK] Track is CLOSED - recentering to (0, 0)" << std::endl;
                        recenterTrack(*m_points, center_info);
                        
                        // ✅ ПРАВИЛЬНОЕ ОБНОВЛЕНИЕ ORIGIN (UTM + GPS):
                        // Точки смещены на offset = -center
                        // Origin UTM должен сместиться в обратную сторону
                        g_map_origin.m_origin_meters_easting -= center_info.offset.x * MapConstants::MAP_SIZE;
                        g_map_origin.m_origin_meters_northing -= center_info.offset.y * MapConstants::MAP_SIZE;
                        
                        // Конвертируем новый UTM origin в GPS
                        try {
                            using namespace GeographicLib;
                            UTMUPS::Reverse(
                                g_map_origin.m_origin_zone_int, 
                                true,  // northp
                                g_map_origin.m_origin_meters_easting,
                                g_map_origin.m_origin_meters_northing,
                                g_map_origin.m_origin_lat_dd,
                                g_map_origin.m_origin_lon_dd
                            );
                        }
                        catch (const std::exception& e) {
                            std::cerr << "[TRACK] GeographicLib Error: " << e.what() << std::endl;
                        }
                        
                        std::cout << "[TRACK] Origin updated:" << std::endl;
                        std::cout << "  UTM: easting=" << g_map_origin.m_origin_meters_easting 
                                  << ", northing=" << g_map_origin.m_origin_meters_northing << std::endl;
                        std::cout << "  GPS: lat=" << g_map_origin.m_origin_lat_dd 
                                  << ", lon=" << g_map_origin.m_origin_lon_dd << std::endl;
                    }
                    else
                    {
                        std::cout << "[TRACK] Track is OPEN - keeping original position" << std::endl;
                    }
                }
                
                // Rebuild track cache for rendering
                TrackRenderer::rebuildTrackCache(*m_points, *m_pointsMutex);
                
                m_showSplash = false;
                m_closeSplash = true;
            }
            else
            {
                std::cerr << "[UI] Failed to open file: " << m_recentFiles[i].path << "\n";
            }
        }
    }
    
    // === BOTTOM BAR ===
    // float bottomBarY = windowSize.y - 40; // Already defined
    float contentY = bottomBarY + 12; // Offset content below the line
    
    // Contact Us
    ImGui::SetCursorPos(ImVec2(25, contentY));
    if (m_iconContact)
    {
        ImGui::Image((ImTextureID)m_iconContact, ImVec2(20, 20));
        ImGui::SameLine();
    }
    ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), " Contact Us");
    
    ImGui::SetCursorPos(ImVec2(25, contentY));
    if (ImGui::InvisibleButton("##contact", ImVec2(140, 30)))
    {
        std::cout << "[UI] Contact Us\n";
    }
    
    // Copyright
    ImGui::SetCursorPos(ImVec2(215, contentY));
    if (m_iconCopyright)
    {
        ImGui::Image((ImTextureID)m_iconCopyright, ImVec2(20, 20));
        ImGui::SameLine();
    }
    ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), " Donate");
    
    ImGui::SetCursorPos(ImVec2(455, contentY));
    if (ImGui::InvisibleButton("##donate", ImVec2(130, 30)))
    {
        std::cout << "[UI] Donate\n";
    }
    
    // Close App
    ImGui::SetCursorPos(ImVec2(windowSize.x - 155, contentY));
    if (m_iconClose)
    {
        ImGui::Image((ImTextureID)m_iconClose, ImVec2(20, 20));
        ImGui::SameLine();
    }
    ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), " Close App");
    
    ImGui::SetCursorPos(ImVec2(windowSize.x - 155, contentY));
    if (ImGui::InvisibleButton("##close", ImVec2(130, 30)))
    {
        std::cout << "[UI] Close App\n";
        glfwSetWindowShouldClose(m_window, true);
    }
    
    ImGui::End();
    ImGui::PopStyleVar();
}

// COMPLETE FIXED UI Menu functions - Blender style

void UI::RenderTopMenu()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 display_size = viewport->Size;
    
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, UIConfig::TOP_MENU_HEIGHT * display_size.y));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    // === TOP MENU BAR STYLING ===
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(
        UIConfig::MENU_FRAME_PADDING_X * display_size.x, 
        UIConfig::MENU_FRAME_PADDING_Y * display_size.y
    ));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(
        UIConfig::MENU_ITEM_SPACING, 
        0
    ));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(
        8.0f / 1600.0f * display_size.x, 
        4.0f / 900.0f * display_size.y
    ));


    // Top menu bar colors
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIConfig::MENU_BG_R, UIConfig::MENU_BG_G, UIConfig::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(UIConfig::MENU_BG_R, UIConfig::MENU_BG_G, UIConfig::MENU_BG_B, 1.0f));
    
    // Dropdown menu colors (применяем настройки из UI_Config.h)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(UIConfig::DROPDOWN_TEXT_R, UIConfig::DROPDOWN_TEXT_G, UIConfig::DROPDOWN_TEXT_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(UIConfig::DROPDOWN_BG_R, UIConfig::DROPDOWN_BG_G, UIConfig::DROPDOWN_BG_B, UIConfig::DROPDOWN_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(UIConfig::DROPDOWN_ACTIVE_R, UIConfig::DROPDOWN_ACTIVE_G, UIConfig::DROPDOWN_ACTIVE_B, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(UIConfig::DROPDOWN_HOVER_R, UIConfig::DROPDOWN_HOVER_G, UIConfig::DROPDOWN_HOVER_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(UIConfig::DROPDOWN_ACTIVE_R, UIConfig::DROPDOWN_ACTIVE_G, UIConfig::DROPDOWN_ACTIVE_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(UIConfig::DROPDOWN_SEPARATOR_R, UIConfig::DROPDOWN_SEPARATOR_G, UIConfig::DROPDOWN_SEPARATOR_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(UIConfig::DROPDOWN_BORDER_R, UIConfig::DROPDOWN_BORDER_G, UIConfig::DROPDOWN_BORDER_B, 1.0f));
    
    ImGui::Begin("##TopMenu", nullptr, window_flags);
    
    
    
    
    
    if (ImGui::BeginMenuBar())
    {
        ImGui::PushFont(m_fontRegular); // Ubuntu Regular
        
        // === ОТСТУП ПЕРВОГО ЭЛЕМЕНТА МЕНЮ ОТ ЛЕВОГО КРАЯ ===
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + UIConfig::MENU_LEFT_PADDING * display_size.x);
        
        // === DROPDOWN MENU ITEM STYLING (применяем настройки для пунктов меню) ===
        // Временно изменяем глобальные стили для dropdown меню
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 old_window_padding = style.WindowPadding;
        ImVec2 old_window_min_size = style.WindowMinSize;
        float old_window_rounding = style.WindowRounding;
        float old_popup_rounding = style.PopupRounding;
        float old_popup_border_size = style.PopupBorderSize;
        
        // Применяем настройки dropdown из UI_Config.h (преобразуем ratio в pixels)
        style.WindowPadding = ImVec2(
            UIConfig::DROPDOWN_PADDING_X * display_size.x, 
            UIConfig::DROPDOWN_PADDING_Y * display_size.y
        );
        style.WindowMinSize = ImVec2(UIConfig::DROPDOWN_MIN_WIDTH * display_size.x, 0.0f);
        style.WindowRounding = UIConfig::DROPDOWN_ROUNDING;
        style.PopupRounding = UIConfig::DROPDOWN_ROUNDING;
        style.PopupBorderSize = UIConfig::DROPDOWN_BORDER_SIZE;
        
        
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(
            UIConfig::DROPDOWN_ITEM_SPACING_X * display_size.x, 
            UIConfig::DROPDOWN_ITEM_SPACING_Y * display_size.y
        ));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(
            UIConfig::DROPDOWN_ITEM_INNER_SPACING * display_size.x, 
            UIConfig::DROPDOWN_ITEM_INNER_SPACING * display_size.y
        ));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(
            UIConfig::DROPDOWN_ITEM_PADDING_X * display_size.x, 
            UIConfig::DROPDOWN_ITEM_PADDING_Y * display_size.y
        ));
        
        // File menu
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N", false, false)) {}
            
            if (ImGui::MenuItem("Open...", "Ctrl+O"))
            {
                // Open native Windows file dialog in Saves folder
                std::cout << "[UI] Opening file dialog in Saves folder..." << std::endl;
                
                OPENFILENAMEA ofn = {};
                char szFile[260] = {0};
                
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = glfwGetWin32Window(m_window);
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "GPX Files\0*.gpx\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                
                // Set initial directory to Saves folder
                std::string savesPath = "Saves";
                ofn.lpstrInitialDir = savesPath.c_str();
                
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                
                if (GetOpenFileNameA(&ofn))
                {
                    std::cout << "[UI] Selected file: " << ofn.lpstrFile << std::endl;
                    
                    // Load the selected file
                    if (m_points && m_pointsMutex)
                    {
                        std::ifstream file(ofn.lpstrFile);
                        if (file.is_open())
                        {
                            std::stringstream buffer;
                            buffer << file.rdbuf();
                            loadTrackFromData(buffer.str(), *m_points, *m_pointsMutex);
                            
                            // Recenter track to (0, 0) if closed
                            {
                                std::lock_guard<std::mutex> lock(*m_pointsMutex);
                                TrackCenterInfo center_info = calculateTrackCenter(*m_points);
                                
                                if (center_info.is_closed)
                                {
                                    std::cout << "[TRACK] Track is CLOSED - recentering to (0, 0)" << std::endl;
                                    recenterTrack(*m_points, center_info);
                                    
                                    g_map_origin.m_origin_lat_dd += center_info.offset.y * (MapConstants::MAP_SIZE / 100000.0);
                                    g_map_origin.m_origin_lon_dd += center_info.offset.x * (MapConstants::MAP_SIZE / 100000.0);
                                    std::cout << "[TRACK] Origin updated to: (" << g_map_origin.m_origin_lat_dd << ", " << g_map_origin.m_origin_lon_dd << ")" << std::endl;
                                }
                                else
                                {
                                    std::cout << "[TRACK] Track is OPEN - keeping original position" << std::endl;
                                }
                            }
                            
                            TrackRenderer::rebuildTrackCache(*m_points, *m_pointsMutex);
                            
                            m_showSplash = false;
                            m_closeSplash = true;
                        }
                        else
                        {
                            std::cerr << "[UI] Failed to open file: " << ofn.lpstrFile << std::endl;
                        }
                    }
                }
            }
            
            if (ImGui::MenuItem("Save", "Ctrl+S", false, false)) {}
            if (ImGui::MenuItem("Save As...", "Shift+Ctrl+S", false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4", false, false)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Settings"))
        {
            if (ImGui::MenuItem("Preferences", nullptr, false, false)) {}
            if (ImGui::MenuItem("Configuration", nullptr, false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Reset to Defaults", nullptr, false, false)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Zoom In", "+", false, false)) {}
            if (ImGui::MenuItem("Zoom Out", "-", false, false)) {}
            if (ImGui::MenuItem("Reset View", "Home", false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Toggle Fullscreen", "F11", false, false)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Networking"))
        {
            if (ImGui::MenuItem("Connect", "Shift+C", false, false)) {}
            if (ImGui::MenuItem("Disconnect", nullptr, false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Start Server", "Shift+S", false, false)) {}
            if (ImGui::MenuItem("Stop Server", nullptr, false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Server Settings", nullptr, false, false)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard Shortcuts", "F1"))
            {
                m_show_help_modal = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About", nullptr, false, false)) {}
            if (ImGui::MenuItem("Documentation", nullptr, false, false)) {}
            ImGui::EndMenu();
        }
        
        ImGui::PopStyleVar(3); // Pop DROPDOWN_ITEM styles (ItemSpacing, ItemInnerSpacing, FramePadding)
        
        // Восстанавливаем оригинальные стили
        style.WindowPadding = old_window_padding;
        style.WindowMinSize = old_window_min_size;
        style.WindowRounding = old_window_rounding;
        style.PopupRounding = old_popup_rounding;
        style.PopupBorderSize = old_popup_border_size;
        
        ImGui::PopFont();
        ImGui::EndMenuBar();
    }
    
    ImGui::End();
    ImGui::PopStyleColor(9); // Pop 9 colors
    ImGui::PopStyleVar(6); // Pop 6 style vars (WindowPadding, WindowBorderSize, WindowRounding, FramePadding, ItemSpacing, ItemInnerSpacing)
}

void UI::RenderBottomMenu()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 display_size = viewport->Size;
    
    float bottom_height = UIConfig::BOTTOM_MENU_HEIGHT * display_size.y;
    ImVec2 bottom_pos = ImVec2(viewport->Pos.x, viewport->Pos.y + display_size.y - bottom_height);
    
    ImGui::SetNextWindowPos(bottom_pos);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, bottom_height));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoSavedSettings | 
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIConfig::MENU_BG_R, UIConfig::MENU_BG_G, UIConfig::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    
    ImGui::Begin("##BottomMenu", nullptr, window_flags);
    
    ImGui::PushFont(m_fontRegular);
    
    // Version info on the right
    const char* version_text = UIConfig::APP_VERSION;
    float text_width = ImGui::CalcTextSize(version_text).x;
    float window_width = ImGui::GetWindowWidth();
    
    ImGui::SetCursorPosX(window_width - text_width - 10);
    ImGui::SetCursorPosY(3); // Vertical center
    ImGui::Text("%s", version_text);
    
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void UI::RenderHelpModal()
{
    if (!m_show_help_modal)
        return;
    
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    
    // Darken background - CLICKABLE to close
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);
    ImGui::SetNextWindowBgAlpha(UIConfig::MODAL_OVERLAY_ALPHA);
    
    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                ImGuiWindowFlags_NoSavedSettings;
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, UIConfig::MODAL_OVERLAY_ALPHA));
    if (ImGui::Begin("##ModalBackground", nullptr, bg_flags))
    {
        // Calculate modal size and position
        ImVec2 modal_size = ImVec2(
            UIConfig::HELP_MODAL_WIDTH * display_size.x, 
            UIConfig::HELP_MODAL_HEIGHT * display_size.y
        );
        ImVec2 modal_pos = ImVec2(
            (display_size.x - modal_size.x) * 0.5f,
            (display_size.y - modal_size.y) * 0.5f
        );
        ImVec2 mouse_pos = ImGui::GetMousePos();
        
        bool clicked_outside = ImGui::IsMouseClicked(0) &&
            (mouse_pos.x < modal_pos.x || mouse_pos.x > modal_pos.x + modal_size.x ||
             mouse_pos.y < modal_pos.y || mouse_pos.y > modal_pos.y + modal_size.y);
        
        if (clicked_outside)
        {
            m_show_help_modal = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    
    // Help modal window with scroll - CAPTURE MOUSE
    ImVec2 modal_size = ImVec2(
        UIConfig::HELP_MODAL_WIDTH * display_size.x, 
        UIConfig::HELP_MODAL_HEIGHT * display_size.y
    );
    
    ImGui::SetNextWindowPos(ImVec2(display_size.x * 0.5f, display_size.y * 0.5f), 
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(modal_size);
    ImGui::SetNextWindowFocus(); // Force focus on modal
    
    ImGuiWindowFlags modal_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(
        UIConfig::MODAL_PADDING_X * display_size.x, 
        UIConfig::MODAL_PADDING_Y * display_size.y
    ));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(
        UIConfig::MODAL_ITEM_SPACING_X * display_size.x, 
        UIConfig::MODAL_ITEM_SPACING_Y * display_size.y
    ));
    
    // ПРИМЕНЯЕМ НАСТРОЙКИ ИЗ UI_CONFIG.H
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIConfig::MODAL_BG_R, UIConfig::MODAL_BG_G, UIConfig::MODAL_BG_B, UIConfig::MODAL_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(UIConfig::MODAL_TEXT_R, UIConfig::MODAL_TEXT_G, UIConfig::MODAL_TEXT_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(UIConfig::MODAL_TITLE_BG_R, UIConfig::MODAL_TITLE_BG_G, UIConfig::MODAL_TITLE_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(UIConfig::MODAL_TITLE_ACTIVE_R, UIConfig::MODAL_TITLE_ACTIVE_G, UIConfig::MODAL_TITLE_ACTIVE_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(UIConfig::MODAL_BUTTON_R, UIConfig::MODAL_BUTTON_G, UIConfig::MODAL_BUTTON_B, UIConfig::MODAL_BUTTON_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(UIConfig::MODAL_BUTTON_HOVER_R, UIConfig::MODAL_BUTTON_HOVER_G, UIConfig::MODAL_BUTTON_HOVER_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(UIConfig::MODAL_BUTTON_ACTIVE_R, UIConfig::MODAL_BUTTON_ACTIVE_G, UIConfig::MODAL_BUTTON_ACTIVE_B, 1.0f));
    
    bool modal_open = true;
    if (ImGui::Begin("Keyboard Shortcuts", &modal_open, modal_flags))
    {
        ImGui::PushFont(m_fontUI);  // Use 16px font for modal content
        
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Camera Controls:");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 250.0f / 1600.0f * display_size.x); // Adaptive column width
        
        ImGui::Text("W / Up Arrow");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Move camera up");
        ImGui::NextColumn();
        
        ImGui::Text("S / Down Arrow");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Move camera down");
        ImGui::NextColumn();
        
        ImGui::Text("A / Left Arrow");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Move camera left");
        ImGui::NextColumn();
        
        ImGui::Text("D / Right Arrow");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Move camera right");
        ImGui::NextColumn();
        
        ImGui::Text("Mouse Wheel");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Zoom in/out");
        ImGui::NextColumn();
        
        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Application:");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 250);
        
        ImGui::Text("ESC");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Close application");
        ImGui::NextColumn();
        
        ImGui::Text("F11");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Toggle fullscreen");
        ImGui::NextColumn();
        
        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Network:");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 250);
        
        ImGui::Text("Shift + S");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Toggle server");
        ImGui::NextColumn();
        
        ImGui::Text("Shift + C");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Toggle client");
        ImGui::NextColumn();
        
        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Testing:");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 250.0f / 1600.0f * display_size.x);
        
        ImGui::Text("T");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Create test vehicle");
        ImGui::NextColumn();
        
        ImGui::Columns(1);
        
        ImGui::PopFont();
    }
    ImGui::End();
    
    // Close modal if user clicked X or clicked outside
    if (!modal_open)
    {
        m_show_help_modal = false;
    }
    
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(2);
}



