// UI Menu functions - add to end of UI.cpp

#include "../src/Config.h"
#include <sstream>
#include <iomanip>

void UI::RenderTopMenu()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, UIColors::MENU_HEIGHT));
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIColors::MENU_BG_R, UIColors::MENU_BG_G, UIColors::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(UIColors::MENU_BG_R, UIColors::MENU_BG_G, UIColors::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(UIColors::TEXT_R, UIColors::TEXT_G, UIColors::TEXT_B, 1.0f));
    
    ImGui::Begin("TopMenu", nullptr, window_flags);
    
    if (ImGui::BeginMenuBar())
    {
        // App name
        ImGui::Text("%s", AppInfo::APP_NAME);
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // File menu
        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("New", nullptr, false, false);
            ImGui::MenuItem("Open", nullptr, false, false);
            ImGui::MenuItem("Save", nullptr, false, false);
            ImGui::Separator();
            ImGui::MenuItem("Exit", nullptr, false, false);
            ImGui::EndMenu();
        }
        
        // Settings menu
        if (ImGui::BeginMenu("Settings"))
        {
            ImGui::MenuItem("Preferences", nullptr, false, false);
            ImGui::MenuItem("Configuration", nullptr, false, false);
            ImGui::EndMenu();
        }
        
        // View menu
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Zoom In", nullptr, false, false);
            ImGui::MenuItem("Zoom Out", nullptr, false, false);
            ImGui::MenuItem("Reset View", nullptr, false, false);
            ImGui::EndMenu();
        }
        
        // Networking menu
        if (ImGui::BeginMenu("Networking"))
        {
            ImGui::MenuItem("Connect", nullptr, false, false);
            ImGui::MenuItem("Disconnect", nullptr, false, false);
            ImGui::MenuItem("Server Settings", nullptr, false, false);
            ImGui::EndMenu();
        }
        
        // Help menu
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard Shortcuts"))
            {
                m_show_help_modal = true;
            }
            ImGui::MenuItem("About", nullptr, false, false);
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
    
    ImGui::End();
    ImGui::PopStyleColor(3);
}

void UI::RenderBottomMenu()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 bottom_pos = ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - UIColors::MENU_HEIGHT);
    
    ImGui::SetNextWindowPos(bottom_pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, UIColors::MENU_HEIGHT));
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoSavedSettings;
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIColors::MENU_BG_R, UIColors::MENU_BG_G, UIColors::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(UIColors::TEXT_R, UIColors::TEXT_G, UIColors::TEXT_B, 1.0f));
    
    ImGui::Begin("BottomMenu", nullptr, window_flags);
    
    // Version info
    ImGui::Text("Version: %s", AppInfo::APP_VERSION);
    
    ImGui::End();
    ImGui::PopStyleColor(2);
}

void UI::RenderHelpModal()
{
    if (!m_show_help_modal)
        return;
    
    // Darken background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.75f);
    
    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
    
    ImGui::Begin("ModalBackground", nullptr, bg_flags);
    ImGui::End();
    
    // Help modal window
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 400));
    
    ImGuiWindowFlags modal_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIColors::MENU_BG_R, UIColors::MENU_BG_G, UIColors::MENU_BG_B, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(UIColors::TEXT_R, UIColors::TEXT_G, UIColors::TEXT_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    
    if (ImGui::Begin("Keyboard Shortcuts", &m_show_help_modal, modal_flags))
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Camera Controls:");
        ImGui::Separator();
        ImGui::Text("W / Up Arrow    - Move camera up");
        ImGui::Text("S / Down Arrow  - Move camera down");
        ImGui::Text("A / Left Arrow  - Move camera left");
        ImGui::Text("D / Right Arrow - Move camera right");
        ImGui::Text("Mouse Wheel     - Zoom in/out");
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Application:");
        ImGui::Separator();
        ImGui::Text("ESC             - Close application");
        ImGui::Text("F11             - Toggle fullscreen (500x500 / maximized)");
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Network:");
        ImGui::Separator();
        ImGui::Text("Shift + S       - Toggle server");
        ImGui::Text("Shift + C       - Toggle client");
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Testing:");
        ImGui::Separator();
        ImGui::Text("T               - Create test vehicle with simulation");
        ImGui::Spacing();
        ImGui::Spacing();
        
        if (ImGui::Button("Close", ImVec2(120, 0)))
        {
            m_show_help_modal = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
}
