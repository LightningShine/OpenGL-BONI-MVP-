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
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
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
#include "src/racing/ModeManager/ModeManager.h"
#include "src/vehicle/Vehicle.h"
#include "src/track/TelemetryTrackBuilder.h"

void RenderRaceMenu();


extern int g_focused_vehicle_id;
extern bool g_show_vehicle_names;
bool g_show_autostop_modal = false;

// Windows API for native file dialogs (include AFTER C++ standard library)
#ifdef _WIN32
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
#endif

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
, m_fontUBold(nullptr)
, m_fontTitle(nullptr)
, m_fontRace(nullptr)
, m_fontRobotoMono(nullptr)
, m_fontOswald(nullptr)
, m_fontOswaldBold(nullptr)
, m_fontJetBrainsMono(nullptr)
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
    , m_sessionElapsedMs(0)
    , m_sessionStartTime(std::chrono::steady_clock::now())
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
    if (!LoadTextureFromFile("styles/images/start.png", &m_backgroundTexture, &w, &h))
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
    
    auto loadFont = [&](const char* path, float size) -> ImFont* {
        if (std::filesystem::exists(path))
            return io.Fonts->AddFontFromFileTTF(path, size, &font_config);
        std::cerr << "[UI] Warning: Font not found: " << path << ", using default\n";
        return io.Fonts->AddFontDefault(&font_config);
    };

    m_fontRegular    = loadFont("styles/fonts/Ubuntu/Ubuntu-Regular.ttf", font_size_menu);
    m_fontUI         = loadFont("styles/fonts/Ubuntu/Ubuntu-Regular.ttf", font_size_ui);
    m_fontUBold      = loadFont("styles/fonts/Ubuntu/Ubuntu-Bold.ttf",    font_size_ui);
    m_fontTitle      = loadFont("styles/fonts/Russo_One/RussoOne-Regular.ttf", font_size_title);
    m_fontRace       = loadFont(UIConfig::FONT_PATH_F1, font_size_race);
    m_fontRobotoMono = loadFont(UIConfig::FONT_PATH_ROBOTO_MONO, font_size_title);
    m_fontOswald     = loadFont(UIConfig::FONT_PATH_OSWALD, font_size_title);
    m_fontOswaldBold = loadFont(UIConfig::FONT_PATH_OSWALD_BOLD, font_size_title);
    m_fontJetBrainsMono = loadFont(UIConfig::FONT_PATH_JETBRAINS_MONO, font_size_title);

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
    io.MouseDrawCursor = false; // Disable ImGui software cursor to prevent visual trailing

    LoadResources();

    m_raceDisplay.Initialize(static_cast<uint32_t>(window_width), static_cast<uint32_t>(window_height));
    m_sessionElapsedMs = 0;
    m_sessionStartTime = std::chrono::steady_clock::now();
    
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
    m_ui_elements->setFontRobotoMono(m_fontRobotoMono);
    m_ui_elements->setFontOswald(m_fontOswald);
    m_ui_elements->setFontOswaldBold(m_fontOswaldBold);
    m_ui_elements->setFontJetBrainsMono(m_fontJetBrainsMono);
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

        // Default to the saves directory used by LoadRecentFiles.
        std::string initialDir = "src/saves";
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
        // Add zoom logic support via AppContext logic
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
        {
            float* appZoom = nullptr;
            // glfw user pointer to app context
            void* raw_context = glfwGetWindowUserPointer(m_window);
            if (raw_context)
            {
                // AppContext matches struct layout exactly
                struct AppContextLayout { float* zoom; };
                AppContextLayout* ctx = static_cast<AppContextLayout*>(raw_context);
                if (ctx && ctx->zoom) *ctx->zoom *= 1.1f;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
        {
            float* appZoom = nullptr;
            void* raw_context = glfwGetWindowUserPointer(m_window);
            if (raw_context)
            {
                struct AppContextLayout { float* zoom; };
                AppContextLayout* ctx = static_cast<AppContextLayout*>(raw_context);
                if (ctx && ctx->zoom) *ctx->zoom *= 0.9f;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            void* raw_context = glfwGetWindowUserPointer(m_window);
            if (raw_context)
            {
                struct AppContextLayout { float* zoom; };
                AppContextLayout* ctx = static_cast<AppContextLayout*>(raw_context);
                if (ctx && ctx->zoom) *ctx->zoom = 1.0f; // Reset zoom to default
            }
        }

        // Open file dialog hotkey
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        {
            OPENFILENAMEA ofn = {};
            char szFile[260] = {0};

            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = glfwGetWin32Window(m_window);
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = "All Files\0*.*\0GPX Files\0*.gpx\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;

            std::string savesPath = "src/saves";
            ofn.lpstrInitialDir = savesPath.c_str();

            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameA(&ofn))
            {
                if (m_points && m_pointsMutex)
                {
                    std::ifstream file(ofn.lpstrFile);
                    if (file.is_open())
                    {
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        loadTrackFromData(buffer.str(), *m_points, *m_pointsMutex);

                        {
                            std::lock_guard<std::mutex> lock(*m_pointsMutex);
                            TrackCenterInfo center_info = calculateTrackCenter(*m_points);

                            if (center_info.is_closed)
                            {
                                recenterTrack(*m_points, center_info);

                                const double dx_m = static_cast<double>(center_info.offset.x) * static_cast<double>(MapConstants::MAP_SIZE);
                                const double dy_m = static_cast<double>(center_info.offset.y) * static_cast<double>(MapConstants::MAP_SIZE);

                                g_map_origin.m_origin_meters_easting += dx_m;
                                g_map_origin.m_origin_meters_northing += dy_m;

                                try {
                                    using namespace GeographicLib;
                                    const bool northp = (g_map_origin.m_origin_zone_char >= 'N');
                                    UTMUPS::Reverse(
                                        g_map_origin.m_origin_zone_int,
                                        northp,
                                        g_map_origin.m_origin_meters_easting,
                                        g_map_origin.m_origin_meters_northing,
                                        g_map_origin.m_origin_lat_dd,
                                        g_map_origin.m_origin_lon_dd);
                                }
                                catch (const std::exception& e) {
                                    std::cerr << "[TRACK] Failed to update origin GPS from UTM: " << e.what() << std::endl;
                                }
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

        // Save Results hotkey
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P))
        {
            std::cout << "[UI] Ctrl+P pressed. Save Results action triggered." << std::endl;
            // Place your save results logic here
        }

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

            // Default to the saves directory used by LoadRecentFiles.
            std::string initialDir = "src/saves";
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
            const int currentLapNum = g_race_manager->GetVehicleCurrentLapNumber(trackedVehicleId);
            const int targetLaps = g_race_manager->GetAutoStopLaps();

            m_ui_elements->drawLapTimer(currentLap, lastLap, bestLap, deltaToBest, currentLapNum, targetLaps);
        }
        else
        {
            m_ui_elements->drawLapTimer(0.0f, -1.0f, -1.0f, 0.0f, 0, 0);
        }
    }

    RenderPrototypeToast();
    RenderNetworkingModal();
    RenderAutoStopModal();

    // Render help modal if open
    RenderHelpModal();
}

void UI::RenderRaceStatusBar(ModeManager* modeManager)
{
    if (!modeManager || m_showSplash)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const uint32_t width = static_cast<uint32_t>(io.DisplaySize.x);
    const uint32_t height = static_cast<uint32_t>(io.DisplaySize.y);

    static uint32_t s_last_width = 0;
    static uint32_t s_last_height = 0;
    if (s_last_width != width || s_last_height != height)
    {
        m_raceDisplay.OnScreenResized(width, height);
        s_last_width = width;
        s_last_height = height;
    }

    if (g_race_manager)
    {
        modeManager->SyncWithSessionState(g_race_manager->GetSessionState());

        float max_time = g_race_manager->GetAutoStopSeconds();
        if (max_time > 0.0f) {
            float elapsed = g_race_manager->GetRaceElapsedTime();
            float remaining = std::max(0.0f, max_time - elapsed);
            m_sessionElapsedMs = static_cast<uint32_t>(remaining * 1000.0f);
        } else {
            const float elapsed_seconds = std::max(0.0f, g_race_manager->GetRaceElapsedTime());
            m_sessionElapsedMs = static_cast<uint32_t>(elapsed_seconds * 1000.0f);
        }

        m_raceDisplay.GetStatusBar().UpdateSessionTime(m_sessionElapsedMs);
    }

    m_raceDisplay.Render(*modeManager);

    const RaceStatusBar& status_bar = m_raceDisplay.GetStatusBar();
    const float bar_x = status_bar.GetXPosition();
    const float bar_y = status_bar.GetYPosition();
    const float bar_w = status_bar.GetBarWidth();
    const float bar_h = status_bar.GetBarHeight();

    const std::string system_time = status_bar.GetSystemTimeString();
    const std::string session_time = status_bar.GetSessionTimeString();
    const char* phase_label = modeManager->GetPhaseLabel();

    ImFont* font = m_fontUI ? m_fontUI : ImGui::GetFont();
    ImGui::PushFont(font);

    const ImVec2 system_size = ImGui::CalcTextSize(system_time.c_str());
    const ImVec2 session_size = ImGui::CalcTextSize(session_time.c_str());
    const ImVec2 phase_size = ImGui::CalcTextSize(phase_label);

    const float flag_size = bar_h * 0.6f;
    const float flag_y = bar_y + (bar_h - flag_size) * 0.5f;
    const float flag_rounding = flag_size * 0.15f;

    const float pill_pad_x = bar_h * 0.4f;
    const float flag_to_text = bar_h * 5.0f; 

    const float pill_w = pill_pad_x + flag_size + flag_to_text + phase_size.x + flag_to_text + flag_size + pill_pad_x;
    const float pill_h = bar_h;
    const float pill_x = bar_x + (bar_w - pill_w) * 0.5f;
    const float pill_y = bar_y;
    const float pill_rounding = pill_h * 0.25f;

    const float left_flag_x = pill_x + pill_pad_x;
    const float text_x = left_flag_x + flag_size + flag_to_text;
    const float right_flag_x = text_x + phase_size.x + flag_to_text;

    const float time_margin = bar_h * 1.2f;
    const float system_x = pill_x - time_margin - system_size.x;
    const float session_x = pill_x + pill_w + time_margin;

    const ImU32 pill_bg = IM_COL32(24, 24, 24, 255);
    const ImU32 pill_border = IM_COL32(160, 160, 160, 180);
    const ImU32 flag_fill = IM_COL32(73, 143, 73, 255);
    const ImU32 text_color = IM_COL32(240, 240, 240, 255);

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    draw_list->AddRectFilled(ImVec2(pill_x, pill_y), ImVec2(pill_x + pill_w, pill_y + pill_h), pill_bg, pill_rounding);
    draw_list->AddRect(ImVec2(pill_x, pill_y), ImVec2(pill_x + pill_w, pill_y + pill_h), pill_border, pill_rounding, 0, 1.5f);

    draw_list->AddRectFilled(ImVec2(left_flag_x, flag_y), ImVec2(left_flag_x + flag_size, flag_y + flag_size), flag_fill, flag_rounding);
    draw_list->AddRectFilled(ImVec2(right_flag_x, flag_y), ImVec2(right_flag_x + flag_size, flag_y + flag_size), flag_fill, flag_rounding);

    const float text_y_sys = bar_y + (bar_h - system_size.y) * 0.5f;
    const float text_y_ses = bar_y + (bar_h - session_size.y) * 0.5f;
    const float text_y_phase = bar_y + (bar_h - phase_size.y) * 0.5f;

    draw_list->AddText(ImVec2(system_x, text_y_sys), text_color, system_time.c_str());
    draw_list->AddText(ImVec2(text_x, text_y_phase), text_color, phase_label);
    draw_list->AddText(ImVec2(session_x, text_y_ses), text_color, session_time.c_str());

    ImGui::PopFont();
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

    // Calculate accurate bottom limit
    const float imageBottomH = windowSize.y * (289.0f / 509.0f);

    // === IMAGE ===
    if (m_backgroundTexture)
    {
        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)m_backgroundTexture,
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + imageBottomH), // Image bootom size
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
            ImVec2(windowPos.x + windowSize.x, windowPos.y + imageBottomH), // Black Shape bottom size
            IM_COL32(20, 20, 25, 255),
            ImGui::GetStyle().WindowRounding,
            ImDrawFlags_RoundCornersTop
        );
    }
    
	// === TITLE "RACE APP" ===
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, -10));

	// Position ratios from image (W:557 H:509)
	const float titleX = windowSize.x * (20.0f / 557.0f);
	const float titleRaceY = windowSize.y * (19.0f / 509.0f);
	const float titleAppY = windowSize.y * (65.0f / 509.0f);
	const float titleFontSize = windowSize.y * (48.0f / 509.0f);

	if (m_fontTitle) 
	{
		ImGui::PushFont(m_fontTitle);
		float targetScale = titleFontSize / m_fontTitle->FontSize;
		ImGui::SetWindowFontScale(targetScale);
	}
	else if (m_fontUI) ImGui::PushFont(m_fontUI);

	ImGui::SetCursorPos(ImVec2(titleX, titleRaceY));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
	ImGui::Text("RACE");
	ImGui::PopStyleColor();

	ImGui::SetCursorPos(ImVec2(titleX, titleAppY));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
	ImGui::Text("APP");
	ImGui::PopStyleColor();

	if (m_fontTitle) 
	{
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();
	}
	else if (m_fontUI) ImGui::PopFont();

	ImGui::PopStyleVar();

	// === VERSION ===
	const float versionY = windowSize.y * (19.0f / 509.0f);

	if (m_fontRegular) ImGui::PushFont(m_fontRegular);
	const ImVec2 verSize = ImGui::CalcTextSize(UIConfig::APP_VERSION);
	ImGui::SetCursorPos(ImVec2(windowSize.x - verSize.x - windowSize.x * (14.0f / 557.0f), versionY));
	ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", UIConfig::APP_VERSION);
	if (m_fontRegular) ImGui::PopFont();

	// === AUTHOR NAME ===
	const float authorX = windowSize.x * (20.0f / 557.0f);
	const float authorY = windowSize.y * (260.0f / 509.0f);
	const float authorFontSize = windowSize.y * (12.0f / 509.0f);

	if (m_fontRegular) 
	{
		ImGui::PushFont(m_fontRegular);
		ImGui::SetWindowFontScale(authorFontSize / m_fontRegular->FontSize);
	}
	ImGui::SetCursorPos(ImVec2(authorX, authorY));
	ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Anastasia Korchagina");
	if (m_fontRegular) 
	{
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();
	}
    
	// === CREATE TRACK BUTTON ===
	// Shared gold color constants – also used by DragDrop hover border
	const ImVec4 btnCol        = ImVec4(218.0f/255.0f, 165.0f/255.0f,  64.0f/255.0f, 1.0f);
	const ImVec4 btnHovCol     = ImVec4(238.0f/255.0f, 185.0f/255.0f,  84.0f/255.0f, 1.0f);
	const ImVec4 btnActiveCol  = ImVec4(198.0f/255.0f, 145.0f/255.0f,  44.0f/255.0f, 1.0f);
	const ImU32  ddHoverBorder = IM_COL32(238, 185, 84, 255); // same as btnHovCol

	// Button lives in the left column (before vertical separator at 277/557)
	const float buttonX       = windowSize.x * (14.0f / 557.0f);
	const float buttonW       = windowSize.x * (255.0f / 557.0f);
	const float buttonH       = windowSize.y * (42.0f / 509.0f);
	const float buttonY       = windowSize.y * (307.0f / 509.0f);
	const float buttonFontSz  = windowSize.y * (16.0f / 509.0f);
	const float buttonRounding = windowSize.y * (8.0f / 509.0f);

	ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
	ImGui::PushStyleColor(ImGuiCol_Button,        btnCol);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHovCol);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  btnActiveCol);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, buttonRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.0f, 0.0f));

	// Ubuntu Bold for button label
	ImFont* btnFont = m_fontUBold ? m_fontUBold : (m_fontUI ? m_fontUI : m_fontRegular);
	if (btnFont)
	{
		ImGui::PushFont(btnFont);
		ImGui::SetWindowFontScale(buttonFontSz / btnFont->FontSize);
	}

	if (ImGui::Button("Create Track", ImVec2(buttonW, buttonH)))
	{
		std::cout << "[UI] Create Track clicked\n";
		TelemetryTrackBuilder::Settings s;
		TelemetryTrackBuilder::Start(s);
		std::cout << "[UI] Telemetry track creation mode enabled. Connect prototype and start driving." << std::endl;
		m_showSplash = false;
		m_closeSplash = true;
	}

	if (btnFont)
	{
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();
	}
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(2);

	// === SEPARATORS ===
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImU32 separatorColor = IM_COL32(80, 80, 80, 255);

	const float vertSepX = windowSize.x * (277.0f / 557.0f);
	const float vertSepTopY = windowSize.y * (307.0f / 509.0f);
	const float vertSepBotY = windowSize.y * (477.0f / 509.0f);

	const float horizSepY = windowSize.y * (477.0f / 509.0f);

	// Vertical Line
	drawList->AddLine(
		ImVec2(windowPos.x + vertSepX, windowPos.y + vertSepTopY),
		ImVec2(windowPos.x + vertSepX, windowPos.y + vertSepBotY),
		separatorColor, 4.0f
	);

	// Horizontal Line
	drawList->AddLine(
		ImVec2(windowPos.x, windowPos.y + horizSepY),
		ImVec2(windowPos.x + windowSize.x, windowPos.y + horizSepY),
		separatorColor, 4.0f
	);

	// === DRAG AND DROP ZONE ===
	// Width matches the Create Track button
	const float ddW = buttonW;
	const float ddH = windowSize.y * (110.0f / 509.0f);
	const float ddX = buttonX;
	const float ddY = windowSize.y * (359.0f / 509.0f);

	ImGui::SetCursorPos(ImVec2(ddX, ddY));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	ImGui::BeginChild("##DragDrop", ImVec2(ddW, ddH), false, ImGuiWindowFlags_NoScrollbar);

	// Hover detection
	bool isHovered = ImGui::IsWindowHovered();
	ImU32 borderColor = isHovered ? ddHoverBorder : separatorColor;
	ImVec4 ddTextCol  = isHovered
		? ImVec4(btnHovCol.x, btnHovCol.y, btnHovCol.z, 1.0f)
		: ImVec4(0.835f, 0.835f, 0.835f, 1.0f);

	// Draw Dashed Border – thicker, rarer dashes
	ImVec2 ddMin = ImVec2(windowPos.x + ddX, windowPos.y + ddY);
	ImVec2 ddMax = ImVec2(ddMin.x + ddW, ddMin.y + ddH);
	AddDashedRect(drawList, ddMin, ddMax, borderColor, 2.5f, 14.0f, 10.0f);

	// Draw Download Icon (centered, in lower half)
	if (m_iconDragDrop)
	{
		const float iconW = windowSize.y * (24.0f / 509.0f);
		const float iconH = iconW;
		const float iconX = (ddW - iconW) / 2.0f;
		const float iconY = ddH * (55.0f / 110.0f);

		ImGui::SetCursorPos(ImVec2(iconX, iconY));
		ImGui::Image((ImTextureID)m_iconDragDrop, ImVec2(iconW, iconH));
	}

	// Text (centered, upper half) – color matches hover
	if (m_fontRegular) ImGui::PushFont(m_fontRegular);
	const char* dndText = "Drag and Drop file there";
	ImVec2 textSize = ImGui::CalcTextSize(dndText);
	const float textX = (ddW - textSize.x) / 2.0f;
	const float textY = (ddH * 0.5f - textSize.y) * 0.5f; // centered in top half

	ImGui::SetCursorPos(ImVec2(textX, textY));
	ImGui::TextColored(ddTextCol, "%s", dndText);
	if (m_fontRegular) ImGui::PopFont();

	ImGui::EndChild();
	ImGui::PopStyleColor(1);

	// === RECENT FILES ===
	const float files_x_pos    = windowSize.x * (293.0f / 557.0f);
	// Bottom of recent list aligns with bottom of DragDrop zone
	const float recfiles_bot_y  = ddY + ddH;
	const float recfiles_item_h = windowSize.y * (20.0f / 509.0f);  // tighter rows
	const int   maxFiles        = 6;
	const float recfiles_list_h = maxFiles * recfiles_item_h;
	// Title sits just above the list
	const float recfiles_title_y = recfiles_bot_y - recfiles_list_h - windowSize.y * (18.0f / 509.0f);
	const float recfiles_list_y  = recfiles_bot_y - recfiles_list_h;

	if (m_fontRegular) ImGui::PushFont(m_fontRegular);
	ImGui::SetCursorPos(ImVec2(files_x_pos, recfiles_title_y));
	ImGui::TextColored(ImVec4(0.525f, 0.525f, 0.525f, 1.0f), "Recent Files");
	if (m_fontRegular) ImGui::PopFont();

	// Recent files list
	if (m_fontRegular) ImGui::PushFont(m_fontRegular);
	const float fileIconSz = windowSize.y * (13.0f / 509.0f);
	const float list_w = windowSize.x - files_x_pos - windowSize.x * (8.0f / 557.0f);
	const float textLineHRow = ImGui::GetTextLineHeight();
	if (m_fontRegular) ImGui::PopFont();

	if (m_fontRegular) ImGui::PushFont(m_fontRegular);
	for (size_t i = 0; i < m_recentFiles.size() && i < (size_t)maxFiles; i++)
	{
		const float rowY = recfiles_list_y + i * recfiles_item_h;

		// Invisible hover button first
		ImGui::SetCursorPos(ImVec2(files_x_pos, rowY));
		ImGui::InvisibleButton(("##filehover" + std::to_string(i)).c_str(), ImVec2(list_w, recfiles_item_h));
		bool rowHovered = ImGui::IsItemHovered();
		bool rowClicked = ImGui::IsItemClicked();
		ImVec4 rowTextCol = rowHovered
			? ImVec4(btnHovCol.x, btnHovCol.y, btnHovCol.z, 1.0f)
			: ImVec4(0.835f, 0.835f, 0.835f, 1.0f);

		// Icon vertically centered in row
		const float iconPosX = files_x_pos;
		const float iconPosY = rowY + (recfiles_item_h - fileIconSz) * 0.5f;
		if (m_iconFile)
		{
			ImU32 iconTint = rowHovered
				? IM_COL32((int)(btnHovCol.x*255), (int)(btnHovCol.y*255), (int)(btnHovCol.z*255), 255)
				: IM_COL32(255, 255, 255, 255);
			ImVec2 iMin = ImVec2(windowPos.x + iconPosX, windowPos.y + iconPosY);
			ImVec2 iMax = ImVec2(iMin.x + fileIconSz, iMin.y + fileIconSz);
			drawList->AddImage((ImTextureID)m_iconFile, iMin, iMax, ImVec2(0,0), ImVec2(1,1), iconTint);
		}

		// Label
		ImGui::SetCursorPos(ImVec2(files_x_pos + fileIconSz + 5.0f, rowY + (recfiles_item_h - textLineHRow) * 0.5f));
		ImGui::TextColored(rowTextCol, "%s", m_recentFiles[i].name.c_str());

		if (rowClicked)
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
    if (m_fontRegular) ImGui::PopFont();

    // === BOTTOM BAR ===
    const float bottomBarY = windowSize.y * (477.0f / 509.0f);
    // Raise items slightly inside the bar
    const float contentY = windowSize.y * (481.0f / 509.0f);

    // Bottom bar layout constants
    const float barIconSz = windowSize.y * (16.0f / 509.0f);
    const float barBtnH   = windowSize.y * (20.0f / 509.0f);
    const float iconOffY  = (barBtnH - barIconSz) * 0.5f;

    if (m_fontRegular) ImGui::PushFont(m_fontRegular);
    const float textLineH = ImGui::GetTextLineHeight();
    const float textOffY  = (barBtnH - textLineH) * 0.5f;
    const float gap = 5.0f;
    const ImVec4 barTextCol = ImVec4(0.835f, 0.835f, 0.835f, 1.0f);
    const ImVec4 barHovCol  = ImVec4(btnHovCol.x, btnHovCol.y, btnHovCol.z, 1.0f);

    // barItem with icon tint on hover
    auto barItem = [&](float x, const char* btnId, void* icon, const char* label, float btnW, auto onClick)
    {
        // Measure full item width for hover area
        if (m_fontRegular) ImGui::PushFont(m_fontRegular);
        const float labelW = ImGui::CalcTextSize(label).x;
        if (m_fontRegular) ImGui::PopFont();
        const float totalW = (icon ? barIconSz + gap : 0.0f) + labelW;
        const float hitW   = btnW > 0 ? btnW : totalW + 4.0f;

        // Invisible hit area
        ImGui::SetCursorPos(ImVec2(x, contentY));
        ImGui::InvisibleButton(btnId, ImVec2(hitW, barBtnH));
        bool hov = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImVec4 tint = hov ? barHovCol : ImVec4(1,1,1,1);
        ImVec4 textC = hov ? barHovCol : barTextCol;

        if (icon)
        {
            ImVec2 iMin = ImVec2(windowPos.x + x, windowPos.y + contentY + iconOffY);
            ImVec2 iMax = ImVec2(iMin.x + barIconSz, iMin.y + barIconSz);
            ImU32 iconTint = hov
                ? IM_COL32((int)(btnHovCol.x*255), (int)(btnHovCol.y*255), (int)(btnHovCol.z*255), 255)
                : IM_COL32(255, 255, 255, 255);
            drawList->AddImage((ImTextureID)icon, iMin, iMax, ImVec2(0,0), ImVec2(1,1), iconTint);
        }
        ImGui::SetCursorPos(ImVec2(x + (icon ? barIconSz + gap : 0.0f), contentY + textOffY));
        ImGui::TextColored(textC, "%s", label);

        if (clicked) onClick();
    };

    barItem(windowSize.x * (14.0f  / 557.0f), "##contact", m_iconContact,   "Contact Us",     windowSize.x * (110.0f / 557.0f), [&]{ std::cout << "[UI] Contact Us\n"; });
    barItem(windowSize.x * (148.0f / 557.0f), "##copy",    m_iconCopyright, "RAJAGP",          0.0f,                             [&]{ });
    barItem(windowSize.x * (293.0f / 557.0f), "##donate",  m_iconHeart,     "Donate to Us",   windowSize.x * (110.0f / 557.0f), [&]{ std::cout << "[UI] Donate\n"; });
    barItem(windowSize.x * (430.0f / 557.0f), "##close",   m_iconClose,     "Close App",      windowSize.x * (110.0f / 557.0f), [&]{ std::cout << "[UI] Close App\n"; glfwSetWindowShouldClose(m_window, true); });

    if (m_fontRegular) ImGui::PopFont();

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
                ofn.lpstrFilter = "All Files\0*.*\0GPX Files\0*.gpx\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                
                // Set initial directory to Saves folder
                std::string savesPath = "src/saves";
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

                                    // Keep map origin consistent with the recentered track.
                                    // Track points are in normalized units; converting to meters requires MAP_SIZE.
                                    // Update UTM origin in meters, then recompute lat/lon accurately.
                                    {
                                        const double dx_m = static_cast<double>(center_info.offset.x) * static_cast<double>(MapConstants::MAP_SIZE);
                                        const double dy_m = static_cast<double>(center_info.offset.y) * static_cast<double>(MapConstants::MAP_SIZE);

                                        g_map_origin.m_origin_meters_easting += dx_m;
                                        g_map_origin.m_origin_meters_northing += dy_m;

                                        try {
                                            using namespace GeographicLib;
                                            const bool northp = (g_map_origin.m_origin_zone_char >= 'N');
                                            UTMUPS::Reverse(
                                                g_map_origin.m_origin_zone_int,
                                                northp,
                                                g_map_origin.m_origin_meters_easting,
                                                g_map_origin.m_origin_meters_northing,
                                                g_map_origin.m_origin_lat_dd,
                                                g_map_origin.m_origin_lon_dd);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "[TRACK] Failed to update origin GPS from UTM: " << e.what() << std::endl;
                                        }

                                        std::cout << "[TRACK] Origin updated:" << std::endl;
                                        std::cout << "  UTM: easting=" << g_map_origin.m_origin_meters_easting
                                                  << ", northing=" << g_map_origin.m_origin_meters_northing << std::endl;
                                        std::cout << "  GPS: lat=" << g_map_origin.m_origin_lat_dd
                                                  << ", lon=" << g_map_origin.m_origin_lon_dd << std::endl;
                                    }
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
                std::thread([hwnd = glfwGetWin32Window(m_window)]() {
                    TelemetryPacket p{};
                    p.MagicMarker = PACKET_MAGIC_DATA;
                    p.ID = 4242;
                    p.fixtype = 4;
                    p.speed = 6000;
                    p.acceleration = 0;
                    p.gForceX = 0;
                    p.gForceY = 0;

                    const double baseLat = 37.4219999;
                    const double baseLon = -122.0840575;

                    double e0 = 0.0, n0 = 0.0;
                    int zone = 0;
                    bool northp = true;
                    GeographicLib::UTMUPS::Forward(baseLat, baseLon, zone, northp, e0, n0);

                    const int steps = 1500;
                    const double radiusMeters = 90.0;
                    uint32_t t = 0;

                    for (int i = 0; i <= steps; ++i)
                    {
                        const double a = (static_cast<double>(i) / static_cast<double>(steps)) * (SimulationConstants::TWO_PI);
                        const double e = e0 + std::cos(a) * radiusMeters;
                        const double n = n0 + std::sin(a) * radiusMeters;
                        double lat = 0.0, lon = 0.0;
                        GeographicLib::UTMUPS::Reverse(zone, northp, e, n, lat, lon);
                        p.lat = static_cast<int32_t>(lat * 1e7);
                        p.lon = static_cast<int32_t>(lon * 1e7);
                        p.time = t;
                        processIncomingTelemetry(p);
                        t += 16;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }

                    for (int i = 0; i < 10; ++i)
                    {
                        const double a = 0.0;
                        const double e = e0 + std::cos(a) * radiusMeters;
                        const double n = n0 + std::sin(a) * radiusMeters;
                        double lat = 0.0, lon = 0.0;
                        GeographicLib::UTMUPS::Reverse(zone, northp, e, n, lat, lon);
                        p.lat = static_cast<int32_t>(lat * 1e7);
                        p.lon = static_cast<int32_t>(lon * 1e7);
                        p.time = t;
                        processIncomingTelemetry(p);
                        t += 16;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }).detach();
            }
            ImGui::Separator();

            if (ImGui::MenuItem("Save Results", "Ctrl+P", false, true)) {
                std::cout << "[UI] Menu: Save Results triggered." << std::endl;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4", false, true)) {
                glfwSetWindowShouldClose(m_window, true);
            }
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
            if (ImGui::MenuItem("Zoom In", "+", false, true)) {
                void* raw_context = glfwGetWindowUserPointer(m_window);
                if (raw_context) {
                    struct AppContextLayout { float* zoom; };
                    AppContextLayout* ctx = static_cast<AppContextLayout*>(raw_context);
                    if (ctx && ctx->zoom) *ctx->zoom *= 1.1f;
                }
            }
            if (ImGui::MenuItem("Zoom Out", "-", false, true)) {
                void* raw_context = glfwGetWindowUserPointer(m_window);
                if (raw_context) {
                    struct AppContextLayout { float* zoom; };
                    AppContextLayout* ctx = static_cast<AppContextLayout*>(raw_context);
                    if (ctx && ctx->zoom) *ctx->zoom *= 0.9f;
                }
            }
            if (ImGui::MenuItem("Reset View", "Home", false, true)) {
                void* raw_context = glfwGetWindowUserPointer(m_window);
                if (raw_context) {
                    struct AppContextLayout { float* zoom; };
                    AppContextLayout* ctx = static_cast<AppContextLayout*>(raw_context);
                    if (ctx && ctx->zoom) *ctx->zoom = 1.0f;
                }
            }
            if (ImGui::MenuItem("Reset Map", nullptr, false, true)) {
                if (g_race_manager)
                    g_race_manager->ResetMap();

                simulationStopAll();

                {
                    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
                    g_vehicles.clear();
                }

                g_focused_vehicle_id = -1;
                g_is_map_loaded = false;
                g_track_render_offset = glm::vec2(0.0f, 0.0f);
                g_map_origin.m_origin_lat_dd = 0.0;
                g_map_origin.m_origin_lon_dd = 0.0;
                g_map_origin.m_origin_meters_easting = 0.0;
                g_map_origin.m_origin_meters_northing = 0.0;
                g_map_origin.m_origin_zone_int = 0;
                g_map_origin.m_origin_zone_char = 0;
                g_map_origin.m_map_size = MapConstants::MAP_SIZE;

                if (m_points && m_pointsMutex)
                {
                    std::lock_guard<std::mutex> lock(*m_pointsMutex);
                    m_points->clear();
                }

                TrackRenderer::clearTrackCache();
                TrackRenderer::clearStartFinishLine();
                telemetryResetPpsCounters();
                telemetryResetPrototypeIdMapping();
                TelemetryTrackBuilder::Stop();
            }
            ImGui::Separator();
           ImGui::MenuItem("Prototype panel", nullptr, &m_allowPrototypeToast);
            ImGui::MenuItem("Vehicle names", nullptr, &g_show_vehicle_names);
            if (ImGui::MenuItem("Toggle Fullscreen", "F11", false, false)) {}
            ImGui::EndMenu();
        }

        RenderRaceMenu();
        
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

    ImGuiIO& io         = ImGui::GetIO();
    const ImVec2 dsz    = io.DisplaySize;
    const float  mW     = UIConfig::HELP_MODAL_WIDTH  * dsz.x;
    const float  mH     = UIConfig::HELP_MODAL_HEIGHT * dsz.y;
    const ImVec2 mPos((dsz.x - mW) * 0.5f, (dsz.y - mH) * 0.5f);
    const ImVec2 mEnd(mPos.x + mW, mPos.y + mH);

    // ── colors ─────────────────────────────────────────────────────────────
    const ImVec4 colGold (218.f/255.f, 165.f/255.f, 64.f/255.f, 1.f);
    const ImU32  uGold   = IM_COL32(218, 165, 64, 255);
    const ImU32  uSep    = IM_COL32(55,  55,  55, 255);
    const ImU32  uBadge  = IM_COL32(38,  38,  38, 255);
    const ImU32  uBadgeB = IM_COL32(80,  80,  80, 255);

    // ── overlay (blocks world input) ───────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(dsz);
    ImGui::SetNextWindowBgAlpha(UIConfig::MODAL_OVERLAY_ALPHA);
    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, UIConfig::MODAL_OVERLAY_ALPHA));
    if (ImGui::Begin("##HelpOverlay", nullptr, bg_flags))
    {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(0) &&
            (mouse_pos.x < mPos.x || mouse_pos.x > mEnd.x ||
             mouse_pos.y < mPos.y || mouse_pos.y > mEnd.y))
        {
            m_show_help_modal = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (!m_show_help_modal) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_show_help_modal = false; return; }
    
    // ── modal window ───────────────────────────────────────────────────────
    const float padX   = mW * 0.044f;
    const float padY   = mH * 0.03f;
    const float titleH = mH * 0.11f;

    ImGui::SetNextWindowPos(mPos);
    ImGui::SetNextWindowSize(ImVec2(mW, mH));
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,  1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,     5.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(UIConfig::MODAL_BG_R, UIConfig::MODAL_BG_G, UIConfig::MODAL_BG_B, UIConfig::MODAL_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.22f, 0.22f, 0.22f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,    ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,  ImVec4(0.32f, 0.32f, 0.32f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.50f, 0.50f, 0.50f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ImVec4(0.70f, 0.70f, 0.70f, 1.f));

    bool modal_open = true;
    if (ImGui::Begin("##HelpModal", &modal_open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Custom title bar ───────────────────────────────────────────────
        const float titleBarBot = mPos.y + titleH;
        dl->AddRectFilled(mPos, ImVec2(mEnd.x, titleBarBot),
            IM_COL32((int)(UIConfig::MODAL_TITLE_BG_R*255),
                     (int)(UIConfig::MODAL_TITLE_BG_G*255),
                     (int)(UIConfig::MODAL_TITLE_BG_B*255), 255),
            10.0f, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(mPos.x, titleBarBot), ImVec2(mEnd.x, titleBarBot), uSep, 1.0f);
        // gold left accent bar
        dl->AddRectFilled(ImVec2(mPos.x,        mPos.y + 3.f),
                          ImVec2(mPos.x + 4.0f, titleBarBot - 3.f),
                          uGold, 2.0f);
        // title text
        if (m_fontUBold) ImGui::PushFont(m_fontUBold);
        {
            const char* tTxt = "Keyboard Shortcuts";
            const ImVec2 tSz = ImGui::CalcTextSize(tTxt);
            dl->AddText(ImVec2(mPos.x + padX + 8.f, mPos.y + (titleH - tSz.y) * 0.5f),
                        IM_COL32(235, 235, 235, 255), tTxt);
        }
        if (m_fontUBold) ImGui::PopFont();
        // version text top-right (no badge)
        if (m_fontRegular) ImGui::PushFont(m_fontRegular);
        {
            const char* vTxt = UIConfig::APP_VERSION;
            const ImVec2 vSz = ImGui::CalcTextSize(vTxt);
            dl->AddText(ImVec2(mEnd.x - vSz.x - padX * 0.6f, mPos.y + (titleH - vSz.y) * 0.5f),
                        uGold, vTxt);
        }
        if (m_fontRegular) ImGui::PopFont();

        // ── Scrollable content child ───────────────────────────────────────
        const float btnH    = mH * 0.062f;   // proportional button height
        // content = space between titlebar and close button, with equal padY gaps
        const float contentH = mH - titleH - 3.f * padY - btnH;
        ImGui::SetCursorPos(ImVec2(padX, titleH + padY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 6.f));
        ImGui::BeginChild("##HelpContent", ImVec2(mW - padX * 2, contentH), false, ImGuiWindowFlags_None);

        ImFont* bodyFont = m_fontUI    ? m_fontUI    : ImGui::GetFont();
        ImFont* boldFont = m_fontUBold ? m_fontUBold : bodyFont;

        // fixed column: description always starts here (px from child-window left)
        const float descCol = mW * 0.40f;

        // Section header ─────────────────────────────────────────────────────
        auto sectionHeader = [&](const char* label)
        {
            ImGui::Dummy(ImVec2(0, 6.f));
            ImGui::PushFont(boldFont);
            ImGui::TextColored(colGold, "%s", label);
            ImGui::PopFont();
            const ImVec2 p = ImGui::GetItemRectMin();
            const ImVec2 q = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x,       q.y + 3.f),
                ImVec2(p.x + mW - padX * 2 - 4.f, q.y + 3.f),
                IM_COL32(218, 165, 64, 140), 2.f);
            ImGui::Dummy(ImVec2(0, 3.f));
        };

        // Key-bind row ────────────────────────────────────────────────────────
        // Uses ImGui::Button for the key badge so clipping and padding are handled
        // correctly by ImGui's own layout system.
        auto bindRow = [&](const char* keys, const char* desc)
        {
            // format: "W|Up" → "W / Up"
            char keyDisp[128] = {};
            {
                int di = 0;
                for (int i = 0; keys[i] && di < 125; ++i)
                {
                    if (keys[i] == '|')
                    { keyDisp[di++] = ' '; keyDisp[di++] = '/'; keyDisp[di++] = ' '; }
                    else
                        keyDisp[di++] = keys[i];
                }
            }

            // Key badge via Button (non-interactive, styled dark/flat)
            ImGui::PushFont(boldFont);
            ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding,   0.f);
            ImGui::PushStyleVar  (ImGuiStyleVar_FrameBorderSize,  1.f);
            ImGui::PushStyleVar  (ImGuiStyleVar_FramePadding,     ImVec2(7.f, 3.f));
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.13f, 0.13f, 0.13f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.13f, 0.13f, 0.13f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.38f, 0.38f, 0.38f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.92f, 0.92f, 0.92f, 1.f));
            // unique ID per row to avoid collisions
            ImGui::PushID(desc);
            ImGui::Button(keyDisp);
            ImGui::PopID();
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
            ImGui::PopFont();

            // Description — always starts at fixed column
            ImGui::SameLine(descCol);
            ImGui::PushFont(bodyFont);
            ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.f), "%s", desc);
            ImGui::PopFont();
        };

        // ── CAMERA ────────────────────────────────────────────────────────
        sectionHeader("Camera");
        bindRow("W|Up",           "Move camera up");
        bindRow("S|Down",         "Move camera down");
        bindRow("A|Left",         "Move camera left");
        bindRow("D|Right",        "Move camera right");
        bindRow("Mouse Wheel",    "Zoom in / Zoom out");
        bindRow("+ (Plus)",       "Zoom in");
        bindRow("- (Minus)",      "Zoom out");
        bindRow("Home",           "Reset zoom to default");
        bindRow("R|Left Arrow",   "Rotate view counter-clockwise");
        bindRow("R|Right Arrow",  "Rotate view clockwise");

        // ── FILE ──────────────────────────────────────────────────────────
        sectionHeader("File");
        bindRow("Ctrl+O",         "Open track file (dialog)");
        bindRow("Ctrl+V",         "Paste track from clipboard");
        bindRow("Ctrl+P",         "Save race results to file");
        bindRow("Space",          "Finalize open track recording");

        // ── NETWORK ───────────────────────────────────────────────────────
        sectionHeader("Network");
        bindRow("Shift+S",        "Open Create Server dialog");
        bindRow("Shift+C",        "Open Connect to Server dialog");

        // ── RACE / SIMULATION ─────────────────────────────────────────────
        sectionHeader("Race & Simulation");
        bindRow("T",              "Spawn test simulation vehicle");
        bindRow("Y",              "Run test track recording (circle)");

        // ── APPLICATION ───────────────────────────────────────────────────
        sectionHeader("Application");
        bindRow("F11",            "Toggle fullscreen / windowed");
        bindRow("ESC",            "Close application");

        ImGui::EndChild();
        ImGui::PopStyleVar(); // ItemSpacing

        // ── Close button — anchored exactly padY above modal bottom ───────
        const float btnW = mW * 0.22f;
        ImGui::SetCursorPos(ImVec2((mW - btnW) * 0.5f, mH - padY - btnH));
        ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        colGold);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(238.f/255.f, 185.f/255.f, 84.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(198.f/255.f, 145.f/255.f, 44.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.07f, 0.07f, 0.07f, 1.f));
        if (m_fontUBold) ImGui::PushFont(m_fontUBold);
        if (ImGui::Button("Close", ImVec2(btnW, btnH)))
            m_show_help_modal = false;
        if (m_fontUBold) ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }
    ImGui::End();

    if (!modal_open) m_show_help_modal = false;

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}





bool UI::WantsMouseCapture() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool UI::WantsKeyboardCapture() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void UI::RenderAutoStopModal()
{
    if (!g_show_autostop_modal)
        return;

    ImVec2 display_size = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);
    ImGui::SetNextWindowBgAlpha(UIConfig::MODAL_OVERLAY_ALPHA);

    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, UIConfig::MODAL_OVERLAY_ALPHA));
    if (ImGui::Begin("##AutoStopBackground", nullptr, bg_flags))
    {
        ImVec2 modal_size = ImVec2(UIConfig::HELP_MODAL_WIDTH * display_size.x * 0.8f, UIConfig::HELP_MODAL_HEIGHT * display_size.y * 0.6f);
        ImVec2 modal_pos = ImVec2((display_size.x - modal_size.x) * 0.5f, (display_size.y - modal_size.y) * 0.5f);
        ImVec2 mouse_pos = ImGui::GetMousePos();

        bool clicked_outside = ImGui::IsMouseClicked(0) &&
            (mouse_pos.x < modal_pos.x || mouse_pos.x > modal_pos.x + modal_size.x ||
             mouse_pos.y < modal_pos.y || mouse_pos.y > modal_pos.y + modal_size.y);

        if (clicked_outside)
            g_show_autostop_modal = false;
    }
    ImGui::End();
    ImGui::PopStyleColor();

    ImVec2 modal_size = ImVec2(UIConfig::HELP_MODAL_WIDTH * display_size.x * 0.8f, UIConfig::HELP_MODAL_HEIGHT * display_size.y * 0.6f);
    ImGui::SetNextWindowPos(ImVec2(display_size.x * 0.5f, display_size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(modal_size);
    ImGui::SetNextWindowFocus();

    ImGuiWindowFlags modal_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UIConfig::MODAL_PADDING_X * display_size.x + 10.0f, UIConfig::MODAL_PADDING_Y * display_size.y + 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(UIConfig::MODAL_ITEM_SPACING_X * display_size.x, 15.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIConfig::MODAL_BG_R, UIConfig::MODAL_BG_G, UIConfig::MODAL_BG_B, UIConfig::MODAL_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(UIConfig::MODAL_TITLE_BG_R, UIConfig::MODAL_TITLE_BG_G, UIConfig::MODAL_TITLE_BG_B, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(UIConfig::MODAL_TITLE_ACTIVE_R, UIConfig::MODAL_TITLE_ACTIVE_G, UIConfig::MODAL_TITLE_ACTIVE_B, 1.0f));

    bool modal_open = true;
    if (ImGui::Begin("Auto Stop Conditions", &modal_open, modal_flags))
    {
        ImGui::PushFont(m_fontUI);

        float windowWidth = ImGui::GetWindowSize().x;
        const char* headerText = "Choose when the race will automatically end:";
        float textWidth = ImGui::CalcTextSize(headerText).x;

        ImGui::SetCursorPos(ImVec2((windowWidth - textWidth) * 0.5f, ImGui::GetCursorPosY()));
        ImGui::TextUnformatted(headerText);
        ImGui::Separator();
        ImGui::Spacing();

        // Inputs are 2 steps lighter than background
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(UIConfig::MODAL_BG_R + 0.12f, UIConfig::MODAL_BG_G + 0.12f, UIConfig::MODAL_BG_B + 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(UIConfig::MODAL_BG_R + 0.18f, UIConfig::MODAL_BG_G + 0.18f, UIConfig::MODAL_BG_B + 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(UIConfig::MODAL_BG_R + 0.22f, UIConfig::MODAL_BG_G + 0.22f, UIConfig::MODAL_BG_B + 0.23f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));

        ImGui::Text("Laps:");
        ImGui::SetNextItemWidth(windowWidth * 0.9); // Use available width
        // Explicitly format integer input to be visible and editable easily
        ImGui::InputInt("##laps", &m_autostop_laps, 0, 0);

        ImGui::Spacing();

        ImGui::Text("Time (HH:MM:SS):");
        ImGui::SetNextItemWidth(windowWidth * 0.9);
        ImGui::InputText("##time", m_autostop_time, sizeof(m_autostop_time));

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0xDA / 255.0f, 0xA5 / 255.0f, 0x40 / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0xDA / 255.0f, 0xA5 / 255.0f, 0x40 / 255.0f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0xDA / 255.0f, 0xA5 / 255.0f, 0x40 / 255.0f, 0.75f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f); // More rounded edges
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 10.0f)); // Taller button

        // Slightly bigger button based on the prompt width
        float btnW = 180.0f;
        float btnH = 45.0f; // Adjusted below with frame padding anyway
        ImGui::SetCursorPosX((windowWidth - btnW) * 0.5f);
        if (ImGui::Button("Start Race", ImVec2(btnW, btnH)))
        {
            if (g_race_manager)
            {
                // Parse HH:MM:SS
                int h = 0, m = 0, s = 0;
                float totalSeconds = 0.0f;
                if (sscanf_s(m_autostop_time, "%d:%d:%d", &h, &m, &s) == 3)
                {
                    totalSeconds = static_cast<float>(h * 3600 + m * 60 + s);
                }
                else if (sscanf_s(m_autostop_time, "%d:%d", &m, &s) == 2)
                {
                    totalSeconds = static_cast<float>(m * 60 + s);
                }

                g_race_manager->SetAutoStopConditions(m_autostop_laps, totalSeconds);
                g_race_manager->StartSession();
            }
            g_show_autostop_modal = false;
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImGui::PopFont();
    }
    ImGui::End();

    if (!modal_open)
        g_show_autostop_modal = false;

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
}
