#include "src/input/Input.h"
#include "UI.h"
#include <iostream>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "libraries/include/stb_image.h"

#include "libraries/include/imgui/imgui.h"
#include "libraries/include/imgui/backends/imgui_impl_glfw.h"
#include "libraries/include/imgui/backends/imgui_impl_opengl3.h"

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
    , m_fontRegular(nullptr)
    , m_fontTitle(nullptr)
    , m_fontRace(nullptr)
    , m_backgroundTexture(nullptr)
    , m_iconFile(nullptr)
    , m_iconContact(nullptr)
    , m_iconCopyright(nullptr)
    , m_iconHeart(nullptr)
    , m_iconClose(nullptr)
    , m_iconDragDrop(nullptr)
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
    // ПОЛНЫЙ путь к изображению
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

    // Test recent files
    m_recentFiles.push_back({"333 Track.json", "C:/tracks/333.json"});
    m_recentFiles.push_back({"Rulitis.json", "C:/tracks/rulitis.json"});
    m_recentFiles.push_back({"MONZA CIRCUIT.json", "C:/tracks/monza.json"});
    m_recentFiles.push_back({"CIRCUIT DE CALAFAT.json", "C:/tracks/calafat.json"});
    m_recentFiles.push_back({"Bikernieku Trase.json", "C:/tracks/bikernieku.json"});
    m_recentFiles.push_back({"NoName.json", "C:/tracks/noname.json"});
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
    
    // Load Fonts
    m_fontRegular = io.Fonts->AddFontFromFileTTF("styles/fonts/JetBrains Mono/ttf/JetBrainsMono-Regular.ttf", 18.0f);
    m_fontTitle = io.Fonts->AddFontFromFileTTF("styles/fonts/JetBrains Mono/ttf/JetBrainsMono-Bold.ttf", 18.0f);
    m_fontRace = io.Fonts->AddFontFromFileTTF("styles/fonts/F1-Font-Family/Formula1-Regular-1.ttf", 18.0f);
    
    if (!m_fontRegular) std::cerr << "[UI] Failed to load Regular font\n";
    if (!m_fontTitle) std::cerr << "[UI] Failed to load Bold font\n";
    if (!m_fontRace) std::cerr << "[UI] Failed to load F1 font\n";

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
    
    // Style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 15.0f;
    style.FrameRounding = 12.0f;
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(8, 8);
    style.WindowBorderSize = 0.0f;
    
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_Text] = ImVec4(0.835f, 0.835f, 0.835f, 1.0f); // D5D5D5
    colors[ImGuiCol_Button] = ImVec4(0.0f, 0.722f, 0.745f, 1.0f); // 00B8BE
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.722f, 0.745f, 1.0f); // Same - no hover effect
    colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.65f, 0.68f, 1.0f);
    
    // Disable hover effect for selectables
    colors[ImGuiCol_Header] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor = true; // ImGui will draw the program cursor
    
    LoadResources();
    
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
    if (m_showSplash)
    {
        RenderSplashWindow();
    }
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
    
    if (m_fontRace) ImGui::PushFont(m_fontRace);
    else if (m_fontTitle) ImGui::PushFont(m_fontTitle);

    ImGui::SetCursorPos(ImVec2(35, 35));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // White color RACE APP color
    ImGui::SetWindowFontScale(3.5f);
    ImGui::Text("RACE");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    
    ImGui::SetCursorPos(ImVec2(35, 110));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(3.5f);
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
    ImGui::SetCursorPos(ImVec2(435, windowSize.y - 290));
    ImGui::TextColored(ImVec4(0.525f, 0.525f, 0.525f, 1.0f), "Recent Files");
    
    // Recent files list
    float listStartY = windowSize.y - 255;
    for (size_t i = 0; i < m_recentFiles.size() && i < 6; i++)
    {
        ImGui::SetCursorPos(ImVec2(435, listStartY + i * 30));
        
        // Draw File Icon
        if (m_iconFile)
        {
            ImGui::Image((ImTextureID)m_iconFile, ImVec2(18, 18));
        }
        
        ImGui::SameLine();
        ImGui::SetCursorPosX(465);
        ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), m_recentFiles[i].name.c_str());
        
        // Clickable area
        ImGui::SetCursorPos(ImVec2(435, listStartY + i * 30));
        if (ImGui::InvisibleButton(("##file" + std::to_string(i)).c_str(), ImVec2(390, 28)))
        {
            std::cout << "[UI] Opening: " << m_recentFiles[i].path << "\n";
            m_showSplash = false;
            m_closeSplash = true;
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
    ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), " Copyright by us");
    
    ImGui::SetCursorPos(ImVec2(215, contentY));
    if (ImGui::InvisibleButton("##copyright", ImVec2(180, 30)))
    {
        std::cout << "[UI] Copyright\n";
    }
    
    // Donate
    ImGui::SetCursorPos(ImVec2(455, contentY));
    if (m_iconHeart)
    {
        ImGui::Image((ImTextureID)m_iconHeart, ImVec2(20, 20));
        ImGui::SameLine();
    }
    ImGui::TextColored(ImVec4(0.835f, 0.835f, 0.835f, 1.0f), " Donate to Us");
    
    ImGui::SetCursorPos(ImVec2(455, contentY));
    if (ImGui::InvisibleButton("##donate", ImVec2(150, 30)))
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