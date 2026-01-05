#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <glm/vec2.hpp>

struct ImGuiContext;
struct ImFont;

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
    void EndFrame();
    
    bool ShouldCloseSplash() const { return m_closeSplash; }
    void CloseSplash() { m_showSplash = false; m_closeSplash = true; }

private:
    GLFWwindow* m_window;
    ImGuiContext* m_context;
    
    bool m_showSplash;
    bool m_closeSplash;
    
    // Fonts
    ImFont* m_fontRegular;
    ImFont* m_fontTitle;
    ImFont* m_fontRace; // Added F1 font
    
    // Textures (using void* instead of ImTextureID to avoid forward declaration issues)
    void* m_backgroundTexture;
    void* m_iconFile;
    void* m_iconContact;
    void* m_iconCopyright;
    void* m_iconHeart;
    void* m_iconClose;
    void* m_iconDragDrop;
    
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

    // Methods
    void RenderSplashWindow();
    void RenderMainWindow();
    
    // Helpers
    bool LoadTextureFromFile(const char* filename, void** out_texture, int* out_width, int* out_height);
    void LoadResources();
};