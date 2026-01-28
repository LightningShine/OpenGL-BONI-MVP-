// Improved UI Menu functions with Blender-like styling

void UI::RenderTopMenu()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, UIColors::MENU_HEIGHT));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    // Push styling - Blender-like dark theme
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIColors::MENU_BG_R, UIColors::MENU_BG_G, UIColors::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(UIColors::MENU_BG_R, UIColors::MENU_BG_G, UIColors::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // Pure white
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.12f, 0.12f, 0.98f)); // Dark popup background
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.4f)); // Hover color
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.6f, 0.9f, 0.6f)); // Brighter hover
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.7f, 1.0f, 0.8f)); // Click color
    
    ImGui::Begin("##TopMenu", nullptr, window_flags);
    
    if (ImGui::BeginMenuBar())
    {
        // App name with padding
        ImGui::SetCursorPosX(10);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", AppInfo::APP_NAME);
        ImGui::Spacing();
        ImGui::Spacing();
        
        // File menu
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N", false, false)) {}
            if (ImGui::MenuItem("Open...", "Ctrl+O", false, false)) {}
            if (ImGui::MenuItem("Save", "Ctrl+S", false, false)) {}
            if (ImGui::MenuItem("Save As...", "Shift+Ctrl+S", false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4", false, false)) {}
            ImGui::EndMenu();
        }
        
        // Settings menu
        if (ImGui::BeginMenu("Settings"))
        {
            if (ImGui::MenuItem("Preferences", nullptr, false, false)) {}
            if (ImGui::MenuItem("Configuration", nullptr, false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Reset to Defaults", nullptr, false, false)) {}
            ImGui::EndMenu();
        }
        
        // View menu
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Zoom In", "+", false, false)) {}
            if (ImGui::MenuItem("Zoom Out", "-", false, false)) {}
            if (ImGui::MenuItem("Reset View", "Home", false, false)) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Toggle Fullscreen", "F11", false, false)) {}
            ImGui::EndMenu();
        }
        
        // Networking menu
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
        
        // Help menu
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
        
        ImGui::EndMenuBar();
    }
    
    ImGui::End();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);
}

void UI::RenderBottomMenu()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 bottom_pos = ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - UIColors::MENU_HEIGHT);
    
    ImGui::SetNextWindowPos(bottom_pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, UIColors::MENU_HEIGHT));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoSavedSettings | 
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIColors::MENU_BG_R, UIColors::MENU_BG_G, UIColors::MENU_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // Pure white
    
    ImGui::Begin("##BottomMenu", nullptr, window_flags);
    
    // Version info on the right
    const char* version_text = AppInfo::APP_VERSION;
    float text_width = ImGui::CalcTextSize(version_text).x;
    float window_width = ImGui::GetWindowWidth();
    
    ImGui::SetCursorPosX(window_width - text_width - 15); // 15px padding from right
    ImGui::SetCursorPosY(7); // Vertical centering
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", version_text);
    
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void UI::RenderHelpModal()
{
    if (!m_show_help_modal)
        return;
    
    // Darken background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.8f); // Darker overlay
    
    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
    ImGui::Begin("##ModalBackground", nullptr, bg_flags);
    ImGui::End();
    ImGui::PopStyleColor();
    
    // Help modal window - Blender style
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), 
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(650, 450));
    
    ImGuiWindowFlags modal_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 15));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.11f, 0.98f)); // Dark background
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // White text
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.75f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.65f, 0.95f, 1.0f));
    
    if (ImGui::Begin("Keyboard Shortcuts", &m_show_help_modal, modal_flags))
    {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Camera Controls:");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 200);
        
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
        ImGui::SetColumnWidth(0, 200);
        
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
        ImGui::SetColumnWidth(0, 200);
        
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
        ImGui::SetColumnWidth(0, 200);
        
        ImGui::Text("T");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Create test vehicle");
        ImGui::NextColumn();
        
        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Center the close button
        float button_width = 120;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_width) * 0.5f);
        if (ImGui::Button("Close", ImVec2(button_width, 30)))
        {
            m_show_help_modal = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(2);
}
