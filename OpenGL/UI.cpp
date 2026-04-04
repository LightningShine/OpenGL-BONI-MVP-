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
#include <regex>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <GeographicLib/UTMUPS.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "libraries/include/stb_image.h"

#include "libraries/include/imgui/imgui.h"
#include "libraries/include/imgui/backends/imgui_impl_glfw.h"
#include "libraries/include/imgui/backends/imgui_impl_opengl3.h"

#include "src/network/Client.h"
#include "src/network/Server.h"
#include "src/network/ESP32_Code.h"
#include "src/network/SimulationServer.h"
#include "src/racing/RaceManager.h"
#include "src/track/TelemetryTrackBuilder.h"

extern int g_focused_vehicle_id;

// Windows API for native file dialogs (include AFTER C++ standard library)
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>  // For GetOpenFileNameA

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#pragma comment(lib, "Ws2_32.lib")

#if NETWORKING_ENABLED
extern bool g_is_server_running;
#endif

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

bool UI::ParseAddressInput(const char* input, std::string& out_host, uint16_t& out_port) const
{
    out_host.clear();
    out_port = 0;

    if (!input)
        return false;

    std::string s(input);
    // trim
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_ws(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_ws(c); }).base(), s.end());

    if (s.empty())
        return false;

    // Allow "local" alias
    if (s == "local" || s == "LOCAL" || s == "Local")
    {
        out_host = "127.0.0.1";
        out_port = m_display_port;
        return true;
    }

    // Expect host:port
    const auto colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size())
        return false;

    out_host = s.substr(0, colon);
    std::string port_str = s.substr(colon + 1);

    // validate port numeric
    for (char c : port_str)
        if (c < '0' || c > '9')
            return false;

    const int port_i = std::atoi(port_str.c_str());
    if (port_i <= 0 || port_i > 65535)
        return false;

    out_port = static_cast<uint16_t>(port_i);

    // validate host: IPv4 or hostname
    if (out_host == "localhost")
        return true;

    // IPv4 quick check
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    if (InetPtonA(AF_INET, out_host.c_str(), &sa.sin_addr) == 1)
        return true;

    // hostname sanity: letters/digits/dot/hyphen
    static const std::regex host_re(R"(^[A-Za-z0-9][A-Za-z0-9\-\.]{0,251}[A-Za-z0-9]$)");
    if (!std::regex_match(out_host, host_re))
        return false;

    return true;
}

void UI::UpdateNetworkingIps()
{
    // Local IP discovery (best-effort)
    m_local_ip.clear();
    {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
        {
            char hostname[256]{};
            if (gethostname(hostname, sizeof(hostname) - 1) == 0)
            {
                addrinfo hints{};
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;
                addrinfo* res = nullptr;
                if (getaddrinfo(hostname, nullptr, &hints, &res) == 0)
                {
                    for (addrinfo* p = res; p; p = p->ai_next)
                    {
                        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                        char ipbuf[INET_ADDRSTRLEN]{};
                        if (inet_ntop(AF_INET, &addr->sin_addr, ipbuf, sizeof(ipbuf)))
                        {
                            std::string ip(ipbuf);
                            if (ip.rfind("127.", 0) != 0)
                            {
                                m_local_ip = ip;
                                break;
                            }
                        }
                    }
                    freeaddrinfo(res);
                }
            }
            WSACleanup();
        }
    }

    // External IP via server UPnP discovery (best-effort; empty if unavailable)
#if NETWORKING_ENABLED
    const char* wan = serverGetWanIp();
    if (wan && wan[0] != 0)
        m_external_ip = wan;
#endif
}

void UI::RenderNetworkingModal()
{
    if (!m_show_networking_modal)
        return;

#if NETWORKING_ENABLED
    // Auto-close on success + surface failures.
    {
        const bool isServer = (m_networkingModalMode == NetworkingModalMode::Server);
        if (isServer)
        {
            if (isServerRunning() && isServerListening())
                m_show_networking_modal = false;
        }
        else
        {
            if (clientIsAuthenticated())
                m_show_networking_modal = false;
            else if (clientHadAuthFailure())
                m_networking_password_invalid = true;
        }
    }
#endif

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;

    // Window placement/size (match connection card)
    const float win_w = display_size.x * (290.0f / 1600.0f);
    const float win_h = display_size.y * (180.0f / 900.0f);
    const ImVec2 win_pos((display_size.x - win_w) * 0.5f, (display_size.y - win_h) * 0.9f);

    // Close on Esc
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_show_networking_modal = false;

    // Colors
    const ImU32 gold = IM_COL32(0xDA, 0xA5, 0x40, 255);
    const ImU32 red = IM_COL32(0x96, 0x00, 0x00, 255);
    const ImU32 border_ok = IM_COL32(0xFF, 0xFF, 0xFF, (int)(255 * 0.21f));
    const ImU32 text_hint = IM_COL32(0xF0, 0xF0, 0xF0, (int)(255 * 0.10f));

    // Networking window (not a popup)
    ImGui::SetNextWindowPos(win_pos);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);

    ImGui::Begin("##NetworkingWindow", nullptr, flags);
    {
        const bool isServer = (m_networkingModalMode == NetworkingModalMode::Server);

        // Deterministic layout in window-local coordinates
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + win_w, p0.y + win_h);
        draw->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 235), 18.0f);
        draw->AddRect(p0, p1, IM_COL32(240, 240, 240, 90), 18.0f, 0, 2.0f);

        const float pad_x = win_w * 0.125f; // (1 - 0.75)/2
        const float field_w = win_w * 0.75f;
        const float header_y = 14.0f;
        const float header_px = 20.0f;
        const float field_h = 34.0f;
        const float gap_1 = 18.0f;
        const float gap_2 = 14.0f;
        const float button_h = 44.0f;
        const float button_y = win_h - 20.0f - button_h;
        const float pass_y = button_y - gap_2 - field_h;
        const float addr_y = pass_y - gap_1 - field_h;

        // Header
        ImFont* titleFont = m_fontTitle ? m_fontTitle : ImGui::GetFont();
        const char* header = isServer ? "Server Creation" : "Server Connection";
        ImGui::PushFont(titleFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0xF0 / 255.0f, 0xF0 / 255.0f, 0xF0 / 255.0f, 1.0f));
        ImGui::SetWindowFontScale(header_px / ImGui::GetFontSize());
        const float header_w = ImGui::CalcTextSize(header).x;
        ImGui::SetCursorPos(ImVec2((win_w - header_w) * 0.5f, header_y));
        ImGui::TextUnformatted(header);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::PopFont();

        auto drawInputFrame = [&](bool invalid) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(min, max, invalid ? red : border_ok, 8.0f, 0, 2.0f);
        };

        // Shared input styling
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0xF0 / 255.0f, 0xF0 / 255.0f, 0xF0 / 255.0f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(text_hint));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 10));

        // Address
        {
            const char* hint = "Server IP: 178.169.20.56:777";
            char hint_buf[128]{};
            if (isServer)
            {
                // Render as plain text when creating server (not an input)
                const char* ip = (!m_external_ip.empty()) ? m_external_ip.c_str() : "<unknown>";
                snprintf(hint_buf, sizeof(hint_buf), "Server IP: %s:%u", ip, (unsigned)m_display_port);

                ImFont* ipFont = m_fontUI ? m_fontUI : ImGui::GetFont();
                ImGui::PushFont(ipFont);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0xF0 / 255.0f, 0xF0 / 255.0f, 0xF0 / 255.0f, 0.90f));
                ImGui::SetCursorPos(ImVec2(pad_x, addr_y + 8.0f));
                ImGui::TextUnformatted(hint_buf);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            if (!isServer)
            {
                ImGui::SetCursorPos(ImVec2(pad_x, addr_y));
                ImGui::SetNextItemWidth(field_w);
                ImGui::InputTextWithHint("##NetworkingAddr", hint, m_networking_addr, sizeof(m_networking_addr));
                drawInputFrame(m_networking_addr_invalid);

                std::string host;
                uint16_t port = 0;
                m_networking_addr_invalid = (m_networking_addr[0] != 0) && !ParseAddressInput(m_networking_addr, host, port);
            }
        }

        // Password
        {
            ImGui::SetCursorPos(ImVec2(pad_x, pass_y));
            ImGui::SetNextItemWidth(field_w);
            ImGui::InputTextWithHint("##NetworkingPassword", "Server Password (Optional)", m_networking_password, sizeof(m_networking_password), ImGuiInputTextFlags_Password);
            drawInputFrame(m_networking_password_invalid);
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        // Button
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(gold));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0xDA / 255.0f, 0xA5 / 255.0f, 0x40 / 255.0f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0xDA / 255.0f, 0xA5 / 255.0f, 0x40 / 255.0f, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 12));

            const char* btn = isServer ? "Create" : "Connect";
            ImGui::SetCursorPos(ImVec2(pad_x, button_y));
            if (ImGui::Button(btn, ImVec2(field_w, button_h)))
            {
                m_networking_addr_invalid = false;
                m_networking_password_invalid = false;

                if (isServer)
                {
#if NETWORKING_ENABLED
                    // Persist server password for auth (empty => allow all)
                    if (m_networking_password[0] == 0)
                        serverSetPassword(nullptr);
                    else
                        serverSetPassword(m_networking_password);

                    if (!isServerRunning())
                    {
                        continueServerRunning();
                        std::thread ServerThread = std::thread(serverWork);
                        ServerThread.detach();
                        ChangeisServerRunning();
                    }
#endif
                    // Password persistence is handled inside server auth flow in existing code.
                }
                else
                {
                    std::string host;
                    uint16_t port = 0;
                    const bool ok = ParseAddressInput(m_networking_addr, host, port);
                    if (!ok)
                    {
                        m_networking_addr_invalid = true;
                    }
                    else
                    {
                        // Optional password: if empty, do not send it.
#if NETWORKING_ENABLED
                        clientClearAuthState();
                        if (m_networking_password[0] == 0)
                            clientSetConnectParams(host.c_str(), port, nullptr);
                        else
                            clientSetConnectParams(host.c_str(), port, m_networking_password);

                        if (!isClientRunning())
                        {
                            continueClientRunning();
                            std::thread ClientThread = std::thread(clientStart);
                            ClientThread.detach();
                            toggleClientRunning();
                        }
                        // If client thread is already running (e.g. waiting for new password),
                        // just updating params above is enough.
#endif
                    }
                }

                // Do not auto-close: keep the card visible so user can see status/errors.
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void UI::RenderPrototypeToast()
{
 // Don't show during initial splash/track selection.
    if (m_showSplash)
        return;

    if (!m_allowPrototypeToast)
        return;

    if (m_lastPrototypeRaceId <= 0)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now > m_prototypeToastUntil)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;

  // Uniform scale from reference design (1600x900). For 16:9 screens,
    // height scaling is sufficient and avoids looking too small at 1080p.
    // Keep the same screen-space proportions as the reference design (1600x900).
    const float win_w_ratio = 270.0f / 1600.0f;
    const float win_h_ratio = 170.0f / 900.0f;
    const ImVec2 win_size(display_size.x * win_w_ratio, display_size.y * win_h_ratio);

    // A uniform scale derived from the window height ratio, for pixel-like offsets.
    const float s = win_size.y / 150.0f;

    const float bottom_menu_h = UIConfig::BOTTOM_MENU_HEIGHT * display_size.y;
    const float margin = 14.0f * s;
    ImVec2 win_pos((display_size.x - win_size.x) * 0.5f,
                  display_size.y - bottom_menu_h - win_size.y - margin);

    ImGui::SetNextWindowPos(win_pos);
    ImGui::SetNextWindowSize(win_size);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f * s);

    ImGui::Begin("##PrototypeToast", nullptr, flags);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p0 = win_pos;
    const ImVec2 p1(win_pos.x + win_size.x, win_pos.y + win_size.y);

    // Background (dark, rounded)
    draw->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 220), 14.0f * s);
    // Subtle border
    draw->AddRect(p0, p1, IM_COL32(240, 240, 240, 40), 14.0f * s, 0, 1.0f * s);

    // Colors
    const ImU32 colHeader = IM_COL32(240, 240, 240, 255);  // #F0F0F0
    const ImU32 colMuted = IM_COL32(134, 134, 134, 255);   // #868686
    const ImU32 colGreen = IM_COL32(0, 132, 60, 255);      // #00843C

    // Fonts scaled from reference sizes
    ImFont* titleFont = m_fontTitle ? m_fontTitle : ImGui::GetFont();
    ImFont* uiFont = m_fontUI ? m_fontUI : ImGui::GetFont();

    const float headerPx = 20.0f * s;
    const float statusPx = 16.0f * s;
    const float batteryPx = 16.0f * s;

    // Header
    {
        char header[64];
        snprintf(header, sizeof(header), "Prototype %d", m_lastPrototypeRaceId);

        ImGui::PushFont(titleFont);
        ImGui::SetWindowFontScale(headerPx / ImGui::GetFontSize());
        ImVec2 tsize = ImGui::CalcTextSize(header);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + (win_size.x - tsize.x) * 0.5f, p0.y + 10.0f * s));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colHeader), "%s", header);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
    }

  // Prototype photo (104x73 in reference)
    {
      const float imgW = 104.0f * s;
        const float imgH = 73.0f * s;
        ImVec2 imgMin(p0.x + (win_size.x - imgW) * 0.5f, p0.y + 28.0f * s);
        ImVec2 imgMax(imgMin.x + imgW, imgMin.y + imgH);
        if (m_protoPhotoTexture)
            draw->AddImage((ImTextureID)m_protoPhotoTexture, imgMin, imgMax);
    }

 float batteryRowY = 0.0f;
    // Battery row (icon 24x24 in reference + percent) - hardcoded for now
    {
        const float rowY = p0.y + 90.0f * s;
        batteryRowY = rowY;
        const float iconW = 24.0f * s;
        const float iconH = 24.0f * s;

        const char* pct = "100%";
        ImGui::PushFont(uiFont);
        ImGui::SetWindowFontScale(batteryPx / ImGui::GetFontSize());
        ImVec2 pctSize = ImGui::CalcTextSize(pct);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();

        const float totalW = iconW + 8.0f * s + pctSize.x;
        ImVec2 iconMin(p0.x + (win_size.x - totalW) * 0.5f, rowY);
        ImVec2 iconMax(iconMin.x + iconW, iconMin.y + iconH);
        if (m_protoBatteryIconTexture)
            draw->AddImage((ImTextureID)m_protoBatteryIconTexture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), colMuted);

        // Battery percent uses Russo One @ 12px (reference)
        ImGui::PushFont(titleFont);
       ImGui::SetCursorScreenPos(ImVec2(iconMax.x + 8.0f * s, rowY + 4.0f * s));
        ImGui::SetWindowFontScale(batteryPx / ImGui::GetFontSize());
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colMuted), "%s", pct);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
    }

    // Connected
    {
        const char* status = "Connected";
     const float gapPx = 1.5f * s;
        ImGui::PushFont(titleFont);
        ImGui::SetWindowFontScale(statusPx / ImGui::GetFontSize());
        ImVec2 stSize = ImGui::CalcTextSize(status);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + (win_size.x - stSize.x) * 0.5f, batteryRowY + 24.0f * s + gapPx));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colGreen), "%s", status);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
    }

    // Pager dots
    {
        const int dots = m_lastPrototypeRaceId;
        if (dots > 1)
        {
            const float dotR = 1.5f * s;
            const float dotGap = 10.0f * s;
            const float dotsW = dots * (dotR * 2.0f) + (dots - 1) * dotGap;
            const float y = p0.y + win_size.y - 16.0f * s;
            float x = p0.x + (win_size.x - dotsW) * 0.5f + dotR;
            for (int i = 0; i < dots; ++i)
            {
                const bool active = (i == dots - 1);
                draw->AddCircleFilled(ImVec2(x, y), dotR, active ? colMuted : IM_COL32(90, 90, 90, 255));
                x += dotR * 2.0f + dotGap;
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
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
 , m_protoBatteryIconTexture(nullptr)
    , m_protoPhotoTexture(nullptr)
    , m_points(nullptr)
    , m_pointsMutex(nullptr)
   , m_showPrototypeToast(true)
    , m_allowPrototypeToast(true)
    , m_lastPrototypeRaceId(0)
    , m_networkingModalMode(NetworkingModalMode::None)
    , m_show_networking_modal(false)
    , m_networking_addr_invalid(false)
    , m_networking_password_invalid(false)
    , m_external_ip("")
    , m_local_ip("")
    , m_display_port(777)
{
    m_networking_addr[0] = 0;
    m_networking_password[0] = 0;
}

UI::~UI()
{
    Shutdown();
}

void UI::CloseSplash()
{
    m_showSplash = false;
    m_closeSplash = true;

    // If a prototype connected while the splash was open, ensure the toast becomes
    // visible after the splash is dismissed.
    if (m_allowPrototypeToast && m_lastPrototypeRaceId > 0)
    {
        m_prototypeToastUntil = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    }
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

    // Prototype toast resources
    // NOTE: Adjust filenames if your asset names differ.
    LoadTextureFromFile("styles/icons/PNG/battery.png", &m_protoBatteryIconTexture, nullptr, nullptr);
    LoadTextureFromFile("styles/images/prototype.png", &m_protoPhotoTexture, nullptr, nullptr);

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
        // Ensure serial threads are stopped before destruction
        stopRealDataCapture();
        stopComPortAutoDiscovery();

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

    // If telemetry builder auto-closed the loop, prompt Save As on the UI thread.
    // Do this early (before WantTextInput guard) so it can't be accidentally skipped.
    if (!m_showSplash && TelemetryTrackBuilder::ConsumeAutoSaveRequest())
    {
        char saveFile[260] = { 0 };
        strncpy_s(saveFile, "track_recorded.txt", _TRUNCATE);
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = glfwGetWin32Window(m_window);
        ofn.lpstrFile = saveFile;
        ofn.nMaxFile = sizeof(saveFile);
        ofn.lpstrFilter = "Track TXT\0*.txt\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        // Default to a user-visible folder (VS working directory can differ).
        std::string initialDir;
        {
            char* userProfile = nullptr;
            size_t len = 0;
            if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile)
            {
                initialDir = std::string(userProfile) + "\\Pictures\\TXT";
                free(userProfile);
            }
            if (initialDir.empty())
                initialDir = ".";
        }
        ofn.lpstrInitialDir = initialDir.c_str();

        std::string chosen;
        if (GetSaveFileNameA(&ofn))
        {
            chosen = ofn.lpstrFile;
        }
        else
        {
            // If user cancels, do not save.
            std::cout << "[UI] Save cancelled by user." << std::endl;
            TelemetryTrackBuilder::Stop(true);
            return;
        }

        // If user cancels, save to default name so we don't lose the created track.
        if (!TelemetryTrackBuilder::SaveFinalizedAsTxt(chosen))
        {
            std::cerr << "[UI] Failed to save finalized track." << std::endl;
        }
        else
        {
            LoadRecentFiles();
            std::cout << "[UI] Track saved." << std::endl;
        }

        // After auto-finish flow, switch mode OFF in UI but keep points for final rendering.
        TelemetryTrackBuilder::Stop(true);
    }

    // Keyboard shortcuts for networking modal
    // Shift+C => client, Shift+S => server
    // Guard: only when not typing in an input field
    ImGuiIO& io = ImGui::GetIO();
    if (!m_showSplash && !io.WantTextInput)
    {
        // Track creation hotkey: Space finalizes an OPEN track (manual finish).
        if (TelemetryTrackBuilder::IsActive() && ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            // Offer Save As dialog so user can name the track.
            char saveFile[260] = { 0 };
            strncpy_s(saveFile, "track_recorded.txt", _TRUNCATE);
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = glfwGetWin32Window(m_window);
            ofn.lpstrFile = saveFile;
            ofn.nMaxFile = sizeof(saveFile);
            ofn.lpstrFilter = "Track TXT\0*.txt\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

            // Default to a user-visible folder (VS working directory can differ).
            std::string initialDir;
            {
                char* userProfile = nullptr;
                size_t len = 0;
                if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile)
                {
                    initialDir = std::string(userProfile) + "\\Pictures\\TXT";
                    free(userProfile);
                }
                if (initialDir.empty())
                    initialDir = ".";
            }
            ofn.lpstrInitialDir = initialDir.c_str();

            std::string chosen;
            if (GetSaveFileNameA(&ofn))
            {
                chosen = ofn.lpstrFile;
            }

            if (!TelemetryTrackBuilder::FinalizeOpenAndSaveTxt(chosen))
            {
                std::cerr << "[UI] Failed to finalize/save open track." << std::endl;
            }
            else
            {
                // Refresh recent files list so the new track appears in splash/recents.
                LoadRecentFiles();
                std::cout << "[UI] Track saved." << std::endl;
            }

            // Manual finish => turn mode OFF.
            TelemetryTrackBuilder::Stop(true);
        }

        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            UpdateNetworkingIps();
            m_networkingModalMode = NetworkingModalMode::Client;
            m_show_networking_modal = true;
            m_networking_addr_invalid = false;
            m_networking_password_invalid = false;
            if (m_networking_addr[0] == 0)
            {
                if (!m_external_ip.empty())
                    snprintf(m_networking_addr, sizeof(m_networking_addr), "%s:%u", m_external_ip.c_str(), (unsigned)m_display_port);
            }
            io.AddInputCharacter(0); // consume
        }
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            UpdateNetworkingIps();
            m_networkingModalMode = NetworkingModalMode::Server;
            m_show_networking_modal = true;
            m_networking_addr_invalid = false;
            m_networking_password_invalid = false;
            if (!m_external_ip.empty())
                snprintf(m_networking_addr, sizeof(m_networking_addr), "%s:%u", m_external_ip.c_str(), (unsigned)m_display_port);
            io.AddInputCharacter(0); // consume
        }
    }
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

    // Live preview while building a track from telemetry.
    // Throttle cache rebuild to avoid heavy CPU/GPU usage.
    static auto s_last_preview_update = std::chrono::steady_clock::now();
    if (TelemetryTrackBuilder::IsActive() && m_points && m_pointsMutex)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - s_last_preview_update >= std::chrono::milliseconds(150))
        {
            s_last_preview_update = now;
            std::vector<glm::vec2> pts = TelemetryTrackBuilder::GetRawPointsSnapshot();
            if (pts.size() >= 2)
            {
                {
                    std::lock_guard<std::mutex> lock(*m_pointsMutex);
                    *m_points = std::move(pts);
                }
                TrackRenderer::rebuildTrackPreviewCache(*m_points, *m_pointsMutex);
            }
        }
    }
    else if (TelemetryTrackBuilder::IsActive())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - s_last_preview_update >= std::chrono::milliseconds(150))
        {
            s_last_preview_update = now;
            std::vector<glm::vec2> pts = TelemetryTrackBuilder::GetRawPointsSnapshot();
            if (pts.size() >= 2)
            {
                static std::mutex s_preview_mutex;
                TrackRenderer::rebuildTrackPreviewCache(pts, s_preview_mutex);
            }
        }
    }

    // If track creation just finished, publish points to renderer buffer.
    static bool s_builder_finished_consumed = false;
    if (TelemetryTrackBuilder::IsFinalized())
    {
        if (!s_builder_finished_consumed && m_points && m_pointsMutex)
        {
            std::vector<glm::vec2> pts = TelemetryTrackBuilder::GetRawPointsSnapshot();
            {
                std::lock_guard<std::mutex> lock(*m_pointsMutex);
                *m_points = std::move(pts);
            }
            TrackRenderer::rebuildTrackCache(*m_points, *m_pointsMutex);
            LoadRecentFiles();
            s_builder_finished_consumed = true;
            std::cout << "[UI] Track creation finalized and rendered." << std::endl;
        }
    }
    else
    {
        s_builder_finished_consumed = false;
    }

    // Lap timer overlay (race info)
    // Use focused vehicle if set, otherwise track leader from standings.
    if (m_ui_elements && g_race_manager)
    {
        int trackedVehicleId = g_focused_vehicle_id;
        if (trackedVehicleId == -1)
        {
            auto standings = g_race_manager->GetStandings();
            if (!standings.empty())
                trackedVehicleId = standings[0].vehicleID;
        }

        if (trackedVehicleId != -1)
        {
            const float currentLap = g_race_manager->GetVehicleCurrentLapTime(trackedVehicleId);
            const float lastLap = g_race_manager->GetVehiclePreviousLapTime(trackedVehicleId);
            const float bestLap = g_race_manager->GetVehicleBestLapTime(trackedVehicleId);
            const float deltaToBest = g_race_manager->GetVehicleLapDelta(trackedVehicleId);

            m_ui_elements->drawLapTimer(currentLap, lastLap, bestLap, deltaToBest);
        }
        else
        {
            m_ui_elements->drawLapTimer(0.0f, -1.0f, -1.0f, 0.0f);
        }
    }

    RenderPrototypeToast();
    RenderNetworkingModal();
    
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

void UI::NotifyPrototypeConnected(int raceVehicleId)
{
    m_lastPrototypeRaceId = raceVehicleId;
    m_prototypeToastUntil = std::chrono::steady_clock::now() + std::chrono::minutes(1);
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
       TelemetryTrackBuilder::Settings s;
        TelemetryTrackBuilder::Start(s);
        std::cout << "[UI] Telemetry track creation mode enabled. Connect prototype and start driving." << std::endl;
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

            // COM port selection (auto-discovered in background thread)
            {
                startComPortAutoDiscovery();

                const std::string selected = getSelectedComPort();
                std::string label = "SELECTED COM PORT: (" + (selected.empty() ? std::string("<none>") : selected) + ")";

                // A hoverable row with a submenu on the right (like in the reference screenshot)
                if (ImGui::BeginMenu(label.c_str()))
                {
                    const auto ports = getAvailableComPorts();
                    if (ports.empty())
                    {
                        ImGui::BeginDisabled();
                        ImGui::MenuItem("<no COM ports detected>", nullptr, false, false);
                        ImGui::EndDisabled();
                    }
                    else
                    {
                        for (const auto& p : ports)
                        {
                            const bool isSelected = (!selected.empty() && p.port == selected);
                            if (ImGui::MenuItem(p.description.c_str(), nullptr, isSelected))
                            {
                                selectAndOpenComPort(p.port);
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            }

            if (ImGui::MenuItem("Reset to Defaults", nullptr, false, false)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Zoom In", "+", false, false)) {}
            if (ImGui::MenuItem("Zoom Out", "-", false, false)) {}
            if (ImGui::MenuItem("Reset View", "Home", false, false)) {}
            ImGui::Separator();
            {
                const bool active = TelemetryTrackBuilder::IsActive();
                const char* label = active ? "Track Creation Mode: ON" : "Track Creation Mode: OFF";
                if (ImGui::MenuItem(label))
                {
                    if (!active)
                    {
                        TelemetryTrackBuilder::Settings s;
                        TelemetryTrackBuilder::Start(s);
                        std::cout << "[UI] Telemetry track creation mode enabled. Waiting for prototype telemetry..." << std::endl;
                    }
                    else
                    {
                        TelemetryTrackBuilder::Stop();
                        std::cout << "[UI] Telemetry track creation mode disabled." << std::endl;
                    }
                }
            }
         if (ImGui::MenuItem("Simulate Prototype Lap (Test)", nullptr, false, TelemetryTrackBuilder::IsActive()))
            {
                // Feed synthetic TelemetryPacket stream into unified telemetry pipeline.
                // This exercises: origin auto-detect, on-the-fly track build, auto-close, recenter, and save.
                std::thread([hwnd = glfwGetWin32Window(m_window)]() {
                    TelemetryPacket p{};
                    p.MagicMarker = PACKET_MAGIC_DATA;
                    p.ID = 4242;
                    p.fixtype = 4;
                    p.speed = 6000;
                    p.acceleration = 0;
                    p.gForceX = 0;
                    p.gForceY = 0;

                    // Fixed origin for simulation. MapOrigin will be created automatically by TelemetryTrackBuilder
                    // on the first packet, so we must ensure the first packet is already on the circle.
                    const double baseLat = 37.4219999;
                    const double baseLon = -122.0840575;

                    // Use UTM around that origin.
                    double e0 = 0.0, n0 = 0.0;
                    int zone = 0;
                    bool northp = true;
                    GeographicLib::UTMUPS::Forward(baseLat, baseLon, zone, northp, e0, n0);

                    const int steps = 1500;
                    const double radiusMeters = 90.0;
                    uint32_t t = 0;

                    // Start exactly on the circle at angle=0 so the first sampled point is part of the loop.
                    for (int i = 0; i <= steps; ++i)
                    {
                        const double a = (static_cast<double>(i) / static_cast<double>(steps)) * (SimulationConstants::TWO_PI);
                        const double e = e0 + std::cos(a) * radiusMeters;
                        const double n = n0 + std::sin(a) * radiusMeters;
                        double lat = 0.0;
                        double lon = 0.0;
                        GeographicLib::UTMUPS::Reverse(zone, northp, e, n, lat, lon);
                        p.lat = static_cast<int32_t>(lat * 1e7);
                        p.lon = static_cast<int32_t>(lon * 1e7);
                        p.time = t;
                        processIncomingTelemetry(p);
                        t += 16;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }

                    // Add a few extra samples at the start position to guarantee close-radius detection.
                    for (int i = 0; i < 10; ++i)
                    {
                        const double a = 0.0;
                        const double e = e0 + std::cos(a) * radiusMeters;
                        const double n = n0 + std::sin(a) * radiusMeters;
                        double lat = 0.0;
                        double lon = 0.0;
                        GeographicLib::UTMUPS::Reverse(zone, northp, e, n, lat, lon);
                        p.lat = static_cast<int32_t>(lat * 1e7);
                        p.lon = static_cast<int32_t>(lon * 1e7);
                        p.time = t;
                        processIncomingTelemetry(p);
                        t += 16;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }

                    // If auto-close didn't happen (settings), let user finish with Space.
                }).detach();
            }
           ImGui::MenuItem("Prototype panel", nullptr, &m_allowPrototypeToast);
            if (ImGui::MenuItem("Toggle Fullscreen", "F11", false, false)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Networking"))
        {
            if (ImGui::MenuItem("Client", "Shift+C"))
            {
                UpdateNetworkingIps();
                m_networkingModalMode = NetworkingModalMode::Client;
                m_show_networking_modal = true;
                m_networking_addr_invalid = false;
                m_networking_password_invalid = false;
                if (m_networking_addr[0] == 0)
                {
                    // Default hint: external_ip:777 (if available)
                    if (!m_external_ip.empty())
                        snprintf(m_networking_addr, sizeof(m_networking_addr), "%s:%u", m_external_ip.c_str(), (unsigned)m_display_port);
                }
            // shown by RenderNetworkingModal()
            }
            if (ImGui::MenuItem("Disconnect"))
            {
#if NETWORKING_ENABLED
                if (isClientRunning())
                {
                    clientStop();
                    clientClearAuthState();
                }
                if (isServerRunning())
                {
                    serverStop();
                }
#endif
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Server", "Shift+S"))
            {
                UpdateNetworkingIps();
                m_networkingModalMode = NetworkingModalMode::Server;
                m_show_networking_modal = true;
                m_networking_addr_invalid = false;
                m_networking_password_invalid = false;
                // Server side shows external_ip:port in the address field.
                if (!m_external_ip.empty())
                    snprintf(m_networking_addr, sizeof(m_networking_addr), "%s:%u", m_external_ip.c_str(), (unsigned)m_display_port);
                // shown by RenderNetworkingModal()
            }
            ImGui::Separator();
            {
                ImGui::BeginDisabled();
#if NETWORKING_ENABLED
                ImGui::MenuItem(isServerRunning() ? "Server: Active" : "Server: Inactive", nullptr, false, false);
#else
                ImGui::MenuItem("Server: Unavailable", nullptr, false, false);
#endif

                if (!m_external_ip.empty())
                    ImGui::MenuItem((std::string("WAN IP: ") + m_external_ip + ":" + std::to_string(m_display_port)).c_str(), nullptr, false, false);

                const char* spw = "";
#if NETWORKING_ENABLED
                spw = serverGetPassword();
#endif
                if (spw && spw[0] != 0)
                    ImGui::MenuItem((std::string("Password: ") + spw).c_str(), nullptr, false, false);
                else
                    ImGui::MenuItem("Password: <none>", nullptr, false, false);

                if (!m_local_ip.empty())
                    ImGui::MenuItem((std::string("Local IP: ") + m_local_ip).c_str(), nullptr, false, false);

                ImGui::EndDisabled();
            }
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

    // Telemetry packets per second on the left
    {
        const uint32_t pps = telemetryGetPacketsPerSecond();
        char pps_text[64];
        snprintf(pps_text, sizeof(pps_text), "PPS: %u", (unsigned)pps);
        ImGui::SetCursorPosX(10);
        ImGui::SetCursorPosY(3);
        ImGui::TextUnformatted(pps_text);
    }
    
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
        ImGui::Spacing();
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Race Results:");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 250.0f / 1600.0f * display_size.x);
        
        ImGui::Text("Ctrl + P");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Save race results to file");
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



