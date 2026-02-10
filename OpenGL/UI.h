#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <glm/vec2.hpp>

struct ImGuiContext;
struct ImFont;
class MapOrigin;
class UIElements;

class UI
{
public:
    UI();
    ~UI();
    
    bool Initialize(GLFWwindow* window);
    void Shutdown();
    
    void SetTrackData(std::vector<glm::vec2>* points, std::mutex* mutex);

    void BeginFrame();
    void Render();
    void RenderTopMenu();
    void RenderBottomMenu();
    void RenderHelpModal();
    void EndFrame();
    
    // Access to UI elements
    UIElements* getElements() { return m_ui_elements; }
    
    // Access to track data
    std::vector<glm::vec2>* getPoints() { return m_points; }
    std::mutex* getPointsMutex() { return m_pointsMutex; }
    
    bool ShouldCloseSplash() const { return m_closeSplash; }
    void CloseSplash() { m_showSplash = false; m_closeSplash = true; }
    
    // Start/Finish line text rendering
    void RenderStartFinishText(const std::vector<glm::vec2>& track_points, 
                              const glm::mat4& view_matrix, 
                              const glm::mat4& projection_matrix,
                              float finish_line_bias = 0.0f);

private:
    GLFWwindow* m_window;
    ImGuiContext* m_context;
    
    bool m_showSplash;
    bool m_closeSplash;
    bool m_show_help_modal;
    
    // Fonts
    ImFont* m_fontRegular;  // 12px for menu
    ImFont* m_fontUI;       // 16px for UI elements (modals, etc.)
    ImFont* m_fontTitle;    // 36px Russo One for LapTimer
    ImFont* m_fontRace;     // F1 font
    
    // Textures (using void* instead of ImTextureID to avoid forward declaration issues)
    void* m_backgroundTexture;
    void* m_iconFile;
    void* m_iconContact;
    void* m_iconCopyright;
    void* m_iconHeart;
    void* m_iconClose;
    void* m_iconDragDrop;
    void* m_compassTexture;
    
    // Recent files
    struct RecentFile
    {
        std::string name;
        std::string path;
    };
    std::vector<RecentFile> m_recentFiles;
    
    // Track Data
    std::vector<glm::vec2>* m_points;
    std::mutex* m_pointsMutex;
    
    // UI Elements (Compass, Laptimer, etc.)
    UIElements* m_ui_elements;

    // Methods
    void RenderSplashWindow();
    void RenderMainWindow();
    
    // Helpers
    bool LoadTextureFromFile(const char* filename, void** out_texture, int* out_width, int* out_height);
    void LoadResources();
    void LoadRecentFiles();
    
private:
    // Helper: Project 3D world position to 2D screen space
    glm::vec2 ProjectToScreen(const glm::vec3& world_pos, 
                             const glm::mat4& view, 
                             const glm::mat4& proj) const;
};