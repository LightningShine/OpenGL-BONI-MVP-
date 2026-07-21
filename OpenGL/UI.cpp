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
#include "libraries/include/imgui/imgui_internal.h" // окна для пересчёта при смене DPI
#include "libraries/include/imgui/backends/imgui_impl_glfw.h"
#include "libraries/include/imgui/backends/imgui_impl_opengl3.h"

#include "src/network/TrackServerClient.h"
#include "src/ui/Accounts.h"
#include "src/ui/pro/ProView.h"
#include "src/network/Server.h"
#include "src/network/ESP32_Code.h"
#include "src/network/SimulationServer.h"
#include "src/racing/RaceManager.h"
#include "src/racing/ModeManager/ModeManager.h"
#include "src/vehicle/Vehicle.h"
#include "src/track/TelemetryTrackBuilder.h"
#include "src/ui/TrackReviewPanel.h"
#include "src/ui/ui_scale.hpp"

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
#include <shellapi.h> // For ShellExecuteA (Ctrl+P print results)

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#pragma comment(lib, "Ws2_32.lib")
#endif

// (GNS globals removed — networking is TrackServerClient now)

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

    // (UPnP/WAN discovery removed with GNS — the track server has a fixed LAN address)
}

// Dispatch track data string to the correct loader (single-edge or dual-edge).
// Handles origin setup, recentering, and GPU upload.
static void applyTrackData(const std::string& data,
    std::vector<glm::vec2>* points, std::mutex* mtx)
{
    std::vector<glm::vec2> left, right;
    if (isDualEdgeFormat(data) && loadDualEdgeFromData(data, left, right))
    {
        TrackRenderer::rebuildTrackCacheFromEdges(left, right);
        return;
    }
    if (!points || !mtx) return;
    loadTrackFromData(data, *points, *mtx);
    {
        std::lock_guard<std::mutex> lk(*mtx);
        TrackCenterInfo ci = calculateTrackCenter(*points);
        if (ci.is_closed)
        {
            recenterTrack(*points, ci);
            g_map_origin.m_origin_meters_easting  -= ci.offset.x * MapConstants::MAP_SIZE;
            g_map_origin.m_origin_meters_northing -= ci.offset.y * MapConstants::MAP_SIZE;
            try {
                using namespace GeographicLib;
                const bool northp = (g_map_origin.m_origin_zone_char >= 'N');
                UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp,
                    g_map_origin.m_origin_meters_easting, g_map_origin.m_origin_meters_northing,
                    g_map_origin.m_origin_lat_dd, g_map_origin.m_origin_lon_dd);
            } catch (...) {}
            // The origin shift above already converts live GPS positions into the
            // recentered frame — keeping recenterTrack's render offset on top of it
            // would shift vehicles a second time (car drawn beside the track).
            g_track_render_offset = glm::vec2(0.0f, 0.0f);
        }
    }
    TrackRenderer::rebuildTrackCache(*points, *mtx);
}

// Load track from file path — handles .trk2 binary and legacy .txt automatically.
static void applyTrackFile(const std::string& path,
    std::vector<glm::vec2>* points, std::mutex* mtx)
{
    const bool isTrk2 = path.size() > 5 &&
        path.compare(path.size() - 5, 5, ".trk2") == 0;
    if (isTrk2) {
        std::vector<glm::vec2> left, right;
        if (loadTrk2File(path, left, right))
            TrackRenderer::rebuildTrackCacheFromEdges(left, right);
        return;
    }
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::stringstream buf; buf << file.rdbuf();
    applyTrackData(buf.str(), points, mtx);
}

void UI::HandleDroppedFile(const std::string& path)
{
    applyTrackFile(path, m_points, m_pointsMutex);
    m_showSplash = false;
    m_closeSplash = true;
}

void UI::RenderNetworkingModal()
{
    if (!m_show_networking_modal)
        return;

    // Auto-close on success + surface failures (Track Server connection).
    if (TrackServerClient::isConnected())
        m_show_networking_modal = false;
    else if (TrackServerClient::hadFailure())
        m_networking_password_invalid = true;

    ImGuiIO& io      = ImGui::GetIO();
    const ImVec2 dsz = io.DisplaySize;

    // Adaptive: width clamped for small/large screens, height derived from content
    // Размеры в пунктах × DPI (ui_scale): карточка держит один физический
    // размер на любом мониторе, а не растёт с разрешением окна.
    float mW = ui_scale::points(520.0f);
    if (mW > dsz.x * 0.9f) mW = dsz.x * 0.9f;
    const float titleH = ui_scale::points(50.0f);
    const float padX   = mW * 0.08f;
    const float padY   = ui_scale::points(20.0f);
    const float fieldH = ui_scale::points(42.0f);
    const float gap    = ui_scale::points(14.0f);
    const float btnH   = ui_scale::points(46.0f);
    const float mH     = titleH + padY + fieldH + gap + fieldH + padY + btnH + padY;
    // Anchored near the bottom: card center at 80% of screen height (20% from bottom edge)
    const ImVec2 mPos((dsz.x - mW) * 0.5f, dsz.y * 0.80f - mH * 0.5f);
    const ImVec2 mEnd(mPos.x + mW, mPos.y + mH);

    // ── colors (Help modal palette) ─────────────────────────────────────────
    const ImVec4 colGold(218.f/255.f, 165.f/255.f, 64.f/255.f, 1.f);
    const ImU32  uGold  = IM_COL32(218, 165, 64, 255);
    const ImU32  uSep   = IM_COL32(55, 55, 55, 255);
    const ImU32  uRed   = IM_COL32(0x96, 0x00, 0x00, 255);
    const ImU32  uFrame = IM_COL32(0xFF, 0xFF, 0xFF, (int)(255 * 0.21f));

    // Free-floating card (no dim overlay); click outside closes
    if (ImGui::IsMouseClicked(0))
    {
        ImVec2 mp = ImGui::GetMousePos();
        if (mp.x < mPos.x || mp.x > mEnd.x || mp.y < mPos.y || mp.y > mEnd.y)
            m_show_networking_modal = false;
    }

    if (!m_show_networking_modal) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_show_networking_modal = false; return; }

    // ── modal window (Help style) ────────────────────────────────────────────
    ImGui::SetNextWindowPos(mPos);
    ImGui::SetNextWindowSize(ImVec2(mW, mH));
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIConfig::MODAL_BG_R, UIConfig::MODAL_BG_G, UIConfig::MODAL_BG_B, UIConfig::MODAL_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.22f, 0.22f, 0.22f, 1.f));

    bool modal_open = true;
    if (ImGui::Begin("##NetworkingModal", &modal_open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Custom title bar (Help style) ──────────────────────────────────
        const float titleBarBot = mPos.y + titleH;
        dl->AddRectFilled(mPos, ImVec2(mEnd.x, titleBarBot),
            IM_COL32((int)(UIConfig::MODAL_TITLE_BG_R * 255),
                     (int)(UIConfig::MODAL_TITLE_BG_G * 255),
                     (int)(UIConfig::MODAL_TITLE_BG_B * 255), 255),
            10.0f, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(mPos.x, titleBarBot), ImVec2(mEnd.x, titleBarBot), uSep, 1.0f);
        dl->AddRectFilled(ImVec2(mPos.x, mPos.y + 3.f), ImVec2(mPos.x + 4.0f, titleBarBot - 3.f), uGold, 2.0f);
        if (m_fontUBold) ImGui::PushFont(m_fontUBold);
        {
            const char* tTxt = "Connect to Track Server";
            const ImVec2 tSz = ImGui::CalcTextSize(tTxt);
            dl->AddText(ImVec2(mPos.x + 14.f, mPos.y + (titleH - tSz.y) * 0.5f),
                        IM_COL32(235, 235, 235, 255), tTxt);
        }
        if (m_fontUBold) ImGui::PopFont();

        auto drawInputFrame = [&](bool invalid) {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                invalid ? uRed : uFrame, 0.0f, 0, 1.5f);
        };

        bool doConnect = false;
        const float fieldW = mW - padX * 2;

        // Shared input styling
        ImGui::PushStyleColor(ImGuiCol_FrameBg,      ImVec4(0.13f, 0.13f, 0.13f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text,         ImVec4(0.94f, 0.94f, 0.94f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(1.f, 1.f, 1.f, 0.25f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, (fieldH - ImGui::GetFontSize()) * 0.5f));

        // Address
        ImGui::SetCursorPos(ImVec2(padX, titleH + padY));
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        doConnect |= ImGui::InputTextWithHint("##NetworkingAddr", "Server IP: 178.169.20.56:777",
            m_networking_addr, sizeof(m_networking_addr), ImGuiInputTextFlags_EnterReturnsTrue);
        drawInputFrame(m_networking_addr_invalid);
        {
            std::string host;
            uint16_t port = 0;
            m_networking_addr_invalid = (m_networking_addr[0] != 0) && !ParseAddressInput(m_networking_addr, host, port);
        }

        // Password
        ImGui::SetCursorPos(ImVec2(padX, titleH + padY + fieldH + gap));
        ImGui::SetNextItemWidth(fieldW);
        doConnect |= ImGui::InputTextWithHint("##NetworkingPassword", "Server Password (Optional)",
            m_networking_password, sizeof(m_networking_password),
            ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        drawInputFrame(m_networking_password_invalid);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        // Enter anywhere in the modal also connects
        doConnect |= ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

        // Connect button (Help-style gold)
        if (m_fontUBold) ImGui::PushFont(m_fontUBold);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        colGold);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(238.f/255.f, 185.f/255.f, 84.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(198.f/255.f, 145.f/255.f, 44.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.07f, 0.07f, 0.07f, 1.f));
        ImGui::SetCursorPos(ImVec2(padX, mH - padY - btnH));
        doConnect |= ImGui::Button("Connect", ImVec2(fieldW, btnH));
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        if (m_fontUBold) ImGui::PopFont();

        if (doConnect)
        {
            m_networking_addr_invalid = false;
            m_networking_password_invalid = false;

            std::string host;
            uint16_t port = 0;
            if (!ParseAddressInput(m_networking_addr, host, port))
            {
                m_networking_addr_invalid = true;
            }
            else
            {
                TrackServerClient::clearFailure();
                TrackServerClient::setConnectParams(host, port, m_networking_password);
                TrackServerClient::start();
            }
            // Keep the card open so errors stay visible; it auto-closes on
            // successful connect (see top of function).
        }
    }
    ImGui::End();

    if (!modal_open) m_show_networking_modal = false;

    ImGui::PopStyleColor(2);
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

    const float bottom_menu_h = UIConfig::bottom_bar_px();
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
, m_fontJBMonoBold(nullptr)
, m_fontRussoSmall(nullptr)
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
    , m_logoTexture(nullptr)
    , m_proMode(false)
    , m_swipeAnim(0.f)
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
    if (!LoadTextureFromFile("styles/icons/PNG/Icon.png",       &m_logoTexture,   nullptr, nullptr)) std::cerr << "Failed to load Icon.png\n";

    // Иконки-цифры 1..9 для боковых групп PRO-вида (512×512 PNG).
    for (int i = 0; i < 9; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "styles/icons/PNG/%d PNG.png", i + 1);
        if (!LoadTextureFromFile(path, &m_numIcons[i], nullptr, nullptr))
            std::cerr << "Failed to load " << path << "\n";
    }
    
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
                
                if (extension == ".json" || extension == ".txt" || extension == ".trk2")
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

/// Загружает все шрифты приложения в атлас ImGui.
/// Размеры — пункты из UIConfig × текущий DPI-масштаб (ui_scale), поэтому
/// текст держит постоянный физический размер и остаётся резким на любом
/// мониторе. Зовётся при старте и повторно при смене DPI.
void UI::load_fonts()
{
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig font_config;
    font_config.OversampleH = 3;
    font_config.OversampleV = 3;

    auto load_font = [&](const char* path, float size_pt) -> ImFont* {
        const float size_px = ui_scale::points(size_pt);
        if (std::filesystem::exists(path))
            return io.Fonts->AddFontFromFileTTF(path, size_px, &font_config);
        std::cerr << "[UI] Warning: Font not found: " << path << ", using default\n";
        return io.Fonts->AddFontDefault(&font_config);
    };

    m_fontRegular    = load_font(UIConfig::FONT_PATH_UBUNTU_REGULAR, UIConfig::FONT_PT_MENU);
    m_fontUI         = load_font(UIConfig::FONT_PATH_UBUNTU_REGULAR, UIConfig::FONT_PT_UI);
    m_fontUBold      = load_font(UIConfig::FONT_PATH_UBUNTU_BOLD,    UIConfig::FONT_PT_UI);
    m_fontTitle      = load_font(UIConfig::FONT_PATH_RUSSO_ONE,      UIConfig::FONT_PT_TITLE);
    m_fontRace       = load_font(UIConfig::FONT_PATH_F1,             UIConfig::FONT_PT_RACE);
    m_fontRobotoMono = load_font(UIConfig::FONT_PATH_ROBOTO_MONO,    UIConfig::FONT_PT_TITLE);
    m_fontOswald     = load_font(UIConfig::FONT_PATH_OSWALD,         UIConfig::FONT_PT_TITLE);
    m_fontOswaldBold = load_font(UIConfig::FONT_PATH_OSWALD_BOLD,    UIConfig::FONT_PT_TITLE);
    m_fontJetBrainsMono = load_font(UIConfig::FONT_PATH_JETBRAINS_MONO, UIConfig::FONT_PT_TITLE);
    m_fontJBMonoBold = load_font(UIConfig::FONT_PATH_JETBRAINS_MONO_BOLD, UIConfig::FONT_PT_TITLE);
    m_fontRussoSmall = load_font(UIConfig::FONT_PATH_RUSSO_ONE,      UIConfig::FONT_PT_RUSSO_SMALL);
}

/// Пересобирает атлас шрифтов под новый DPI-масштаб и подтягивает размеры
/// стиля ImGui. Зовётся из BeginFrame ДО NewFrame — трогать атлас посреди
/// кадра нельзя.
void UI::apply_ui_scale_change()
{
    const float new_scale = ui_scale::get();
    const float ratio = (m_appliedUiScale > 0.0f) ? new_scale / m_appliedUiScale : 1.0f;
    if (ratio != 1.0f)
    {
        ImGui::GetStyle().ScaleAllSizes(ratio);

        // Позиции/размеры окон сохранены в пикселях старого монитора —
        // умножаем на отношение масштабов, чтобы вся раскладка (включая
        // перетащенные PRO-панели) сохранила физическое расположение.
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        for (int i = 0; i < ctx->Windows.Size; ++i)
        {
            ImGuiWindow* win = ctx->Windows[i];
            win->Pos      = ImVec2(win->Pos.x * ratio,      win->Pos.y * ratio);
            win->Size     = ImVec2(win->Size.x * ratio,     win->Size.y * ratio);
            win->SizeFull = ImVec2(win->SizeFull.x * ratio, win->SizeFull.y * ratio);
        }

        // Переезд на монитор с другой плотностью: окно сохраняет ФИЗИЧЕСКИЙ
        // размер — его пиксели пересчитываются тем же коэффициентом, что и
        // контент (на большом редком мониторе окно в пикселях уменьшится).
        // При смене множителя в View → UI Scale монитор тот же — окно не трогаем.
        const bool monitor_changed = (ui_scale::monitor_scale() != m_appliedMonitorScale);
        const bool window_is_free  = !glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) &&
                                     !glfwGetWindowMonitor(m_window); // не fullscreen
        if (monitor_changed && window_is_free)
        {
            int win_w = 0, win_h = 0;
            glfwGetWindowSize(m_window, &win_w, &win_h);
            glfwSetWindowSize(m_window,
                              (int)(win_w * ratio + 0.5f),
                              (int)(win_h * ratio + 0.5f));
        }

        // Переходные кадры: окно ОС меняет размер асинхронно. Замораживаем
        // клэмп PRO-панелей и пропорциональный перенос позиций, пока всё не
        // устаканится — иначе они правят раскладку по рассинхронённому
        // состоянию и панели необратимо уезжают.
        Pro::g_layout_freeze_frames = 3;
    }
    m_appliedMonitorScale = ui_scale::monitor_scale();
    m_appliedUiScale = new_scale;

    // Старая GL-текстура атласа удаляется; бэкенд создаст новую в NewFrame
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui::GetIO().Fonts->Clear();
    load_fonts();
    ImGui::GetIO().Fonts->Build();

    // Указатели ImFont* стали новыми — раздаём их всем держателям
    if (m_ui_elements)
    {
        m_ui_elements->setFontTitle(m_fontTitle);
        m_ui_elements->setFontRobotoMono(m_fontRobotoMono);
        m_ui_elements->setFontOswald(m_fontOswald);
        m_ui_elements->setFontOswaldBold(m_fontOswaldBold);
        m_ui_elements->setFontJetBrainsMono(m_fontJetBrainsMono);
    }

    std::cout << "[UI] Fonts rebuilt for UI scale " << new_scale << std::endl;
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
    
    load_fonts();
    m_appliedUiScale = ui_scale::get();
    m_appliedMonitorScale = ui_scale::monitor_scale();

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
    
    if (!ImGui_ImplOpenGL3_Init("#version 330"))
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
    // Следим за монитором/разрешением (дешёво: полный пересчёт раз в
    // секунду внутри). Смена масштаба коммитится с дебаунсом — см. ui_scale.
    ui_scale::poll(m_window);

    // Масштаб устоялся и изменился — пересобираем шрифты до начала кадра,
    // чтобы текст остался резким, а элементы — того же физического размера.
    if (ui_scale::consume_scale_changed())
        apply_ui_scale_change();

    // Размер окна приложения изменился (смена разрешения, ресайз, переезд):
    // переносим ПОЗИЦИИ окон пропорционально, чтобы раскладка не сбивалась
    // в угол при уменьшении и возвращалась на место при увеличении.
    // Размеры не трогаем — физический размер панелей держит DPI-масштаб.
    {
        int win_w = 0, win_h = 0;
        glfwGetWindowSize(m_window, &win_w, &win_h);
        if (win_w > 0 && win_h > 0)
        {
            if (Pro::g_layout_freeze_frames > 0)
            {
                // DPI-переход: окно ОС ещё догоняет заказанный размер.
                // Никаких поправок — только синхронизируем слежение, иначе
                // поправка по переходному размеру навсегда сдвинет панели.
                --Pro::g_layout_freeze_frames;
            }
            else if (m_prevWinW > 0.0f && ((float)win_w != m_prevWinW || (float)win_h != m_prevWinH))
            {
                const float rx = (float)win_w / m_prevWinW;
                const float ry = (float)win_h / m_prevWinH;
                ImGuiContext* ctx = ImGui::GetCurrentContext();
                for (int i = 0; i < ctx->Windows.Size; ++i)
                {
                    ImGuiWindow* win = ctx->Windows[i];
                    win->Pos = ImVec2(win->Pos.x * rx, win->Pos.y * ry);
                }
            }
            m_prevWinW = (float)win_w;
            m_prevWinH = (float)win_h;
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // If telemetry builder auto-closed the loop, prompt Save As on the UI thread.
    // Do this early (before WantTextInput guard) so it can't be accidentally skipped.
    if (!m_showSplash && TelemetryTrackBuilder::ConsumeAutoSaveRequest())
    {
        const bool isDualEdge = (TelemetryTrackBuilder::GetPhase() == EdgePhase::Done);
        char saveFile[260] = { 0 };
        strncpy_s(saveFile, isDualEdge ? "track_recorded.trk2" : "track_recorded.txt", _TRUNCATE);
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = glfwGetWin32Window(m_window);
        ofn.lpstrFile = saveFile;
        ofn.nMaxFile = sizeof(saveFile);
        ofn.lpstrFilter = isDualEdge
            ? "trk2\0*.trk2\0All Files\0*.*\0"
            : "Track TXT\0*.txt\0All Files\0*.*\0";
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

    // Keyboard shortcut: Shift+C => Connect to Track Server modal
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
            ofn.lpstrFilter = "trk2\0*.trk2\0Track TXT\0*.txt\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;

            std::string savesPath = "src/saves";
            ofn.lpstrInitialDir = savesPath.c_str();

            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameA(&ofn))
            {
                {
                    applyTrackFile(ofn.lpstrFile, m_points, m_pointsMutex);
                    m_showSplash = false;
                    m_closeSplash = true;
                }
            }
        }

        // Ctrl+S — save session results as a .txt file (Save As dialog).
        // Works for local (prototype/COM) and Track Server sessions alike.
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && g_race_manager)
        {
            char saveFile[260] = "RaceResults.txt";
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = glfwGetWin32Window(m_window);
            ofn.lpstrFile = saveFile;
            ofn.nMaxFile = sizeof(saveFile);
            ofn.lpstrFilter = "Text file\0*.txt\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrDefExt = "txt";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
            if (GetSaveFileNameA(&ofn))
            {
                std::ofstream f(ofn.lpstrFile);
                if (f)
                {
                    f << g_race_manager->BuildResultsText();
                    std::cout << "[UI] Results saved to " << ofn.lpstrFile << std::endl;
                }
                else
                {
                    std::cerr << "[UI] Cannot write " << ofn.lpstrFile << std::endl;
                }
            }
        }

        // Ctrl+P — print session results: write the report to a temp .txt and
        // hand it to the standard Windows print flow for text files. Falls
        // back to opening the file if no print handler is registered.
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P) && g_race_manager)
        {
            char tmpDir[MAX_PATH] = {};
            GetTempPathA(MAX_PATH, tmpDir);
            const std::string path = std::string(tmpDir) + "RAJAGP_Results.txt";
            {
                std::ofstream f(path);
                f << g_race_manager->BuildResultsText();
            }
            HINSTANCE r = ShellExecuteA(nullptr, "print", path.c_str(),
                                        nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(r) <= 32)
                ShellExecuteA(nullptr, "open", path.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            std::cout << "[UI] Results sent to print: " << path << std::endl;
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
            m_networkingModalMode = NetworkingModalMode::Connect;
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

    // ── Pro view swipe animation ─────────────────────────────────────────────
    {
        const float dt     = ImGui::GetIO().DeltaTime;
        const float speed  = 10.f;
        const float target = m_proMode ? 1.f : 0.f;
        m_swipeAnim += (target - m_swipeAnim) * (1.f - expf(-speed * dt));
        if (m_swipeAnim < 0.001f) m_swipeAnim = 0.f;
        if (m_swipeAnim > 0.999f) m_swipeAnim = 1.f;
        if (m_swipeAnim > 0.f)
            RenderProView();
    }

    // ── Edge-recording HUD ──────────────────────────────────────────────────
    // One contextual button walks the phases:
    //   LEFT edge → "Finish Left Edge" → TRANSIT (no points recorded)
    //   TRANSIT   → "Start Right Edge" (pressed at the right edge start)
    //   RIGHT edge → "Finish & Review" → review screen
    {
        const EdgePhase phase = TelemetryTrackBuilder::GetPhase();
        if (phase == EdgePhase::Left || phase == EdgePhase::Transit || phase == EdgePhase::Right)
        {
            const ImGuiIO& io = ImGui::GetIO();
            const float w = ui_scale::points(340.f);
            const float h = ui_scale::points(84.f);
            ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - w) * 0.5f, ui_scale::points(16.f)), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.82f);
            ImGui::Begin("##EdgeHUD", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoNav);

            const size_t pointCount   = TelemetryTrackBuilder::PointCount();
            const float  lengthMeters = TelemetryTrackBuilder::LengthMeters();
            if (phase == EdgePhase::Left)
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.f, 1.f),
                    "Recording LEFT edge  —  %zu pts, %.0f m", pointCount, lengthMeters);
            else if (phase == EdgePhase::Transit)
                ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.f),
                    "Transit — drive to the RIGHT edge start");
            else
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.3f, 1.f),
                    "Recording RIGHT edge  —  %zu pts, %.0f m", pointCount, lengthMeters);

            // GPS quality: fix type 4+ means RTK-grade accuracy.
            const int fixType = TelemetryTrackBuilder::LastFixType();
            if (fixType >= 4)
                ImGui::TextColored(ImVec4(0.43f, 0.98f, 0.56f, 1.f), "RTK fix — full accuracy");
            else if (fixType > 0)
                ImGui::TextColored(ImVec4(0.95f, 0.77f, 0.25f, 1.f), "GPS fix %d — reduced accuracy!", fixType);
            else
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "Waiting for GPS data...");

            ImGui::End();

            // Separate clickable window for the action button (NoInputs is off here)
            ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - w) * 0.5f,
                                           ui_scale::points(16.f) + h + ui_scale::points(8.f)), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(w, ui_scale::points(40.f)), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::Begin("##EdgeBtn", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0xDA/255.f, 0xA5/255.f, 0x40/255.f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0xE8/255.f, 0xB8/255.f, 0x55/255.f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0xBB/255.f, 0x85/255.f, 0x20/255.f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);

            const ImVec2 btnSize(w - ui_scale::points(16.f), ui_scale::points(32.f));
            if (phase == EdgePhase::Left)
            {
                if (ImGui::Button("Finish Left Edge", btnSize))
                    TelemetryTrackBuilder::FinishLeftEdge();
            }
            else if (phase == EdgePhase::Transit)
            {
                if (ImGui::Button("Start Right Edge", btnSize))
                    TelemetryTrackBuilder::StartRightEdge();
            }
            else
            {
                if (ImGui::Button("Finish & Review", btnSize))
                    TelemetryTrackBuilder::FinishRightEdge();
            }

            ImGui::PopStyleVar(1);
            ImGui::PopStyleColor(4);
            ImGui::End();
        }
    }


    // Live preview while recording a track edge.
    static auto s_last_preview_update = std::chrono::steady_clock::now();
    if (TelemetryTrackBuilder::IsActive())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - s_last_preview_update >= std::chrono::milliseconds(150))
        {
            s_last_preview_update = now;
            const auto phase = TelemetryTrackBuilder::GetPhase();

            if (phase == EdgePhase::Right)
            {
                // Show the forming track mesh while the right edge is being driven.
                // Both edges are recorded at the same GPS packet rate, so index i
                // in each array corresponds to roughly the same physical position.
                // Using raw slices (not resample) avoids warping the full left edge
                // to match a partially-driven right edge, which caused the "center
                // fills first" artifact.
                auto left  = TelemetryTrackBuilder::GetLeftEdgeSnapshot();
                auto right = TelemetryTrackBuilder::GetRawPointsSnapshot();
                if (left.size() >= 10 && right.size() >= 10)
                {
                    int n = (int)(left.size() < right.size() ? left.size() : right.size());
                    std::vector<glm::vec2> leftPart(left.begin(),  left.begin()  + n);
                    std::vector<glm::vec2> rightPart(right.begin(), right.begin() + n);
                    alignPolylineDirection(rightPart, leftPart);
                    TrackRenderer::rebuildDualEdgePreviewCache(leftPart, rightPart);
                }
                else if (right.size() >= 2)
                {
                    // Too few right points yet — show just the right edge as a thin line.
                    TrackRenderer::rebuildEdgeLineCache(right);
                }
            }
            else if (phase == EdgePhase::Transit)
            {
                // Between edges: keep showing the finished left edge
                std::vector<glm::vec2> left = TelemetryTrackBuilder::GetLeftEdgeSnapshot();
                if (left.size() >= 2)
                    TrackRenderer::rebuildEdgeLineCache(left);
            }
            else
            {
                // Left edge: show single polyline preview
                std::vector<glm::vec2> pts = TelemetryTrackBuilder::GetRawPointsSnapshot();
                if (pts.size() >= 2)
                    TrackRenderer::rebuildEdgeLineCache(pts);
            }
        }
    }

    // Consume finalized track and push to renderer.
    static bool s_builder_finished_consumed = false;
    if (TelemetryTrackBuilder::IsFinalized())
    {
        if (!s_builder_finished_consumed)
        {
            if (TelemetryTrackBuilder::GetPhase() == EdgePhase::Done)
            {
                // Dual-edge finalize
                auto left  = TelemetryTrackBuilder::GetLeftEdgeSnapshot();
                auto right = TelemetryTrackBuilder::GetRawPointsSnapshot();
                TrackRenderer::rebuildTrackCacheFromEdges(left, right);
            }
            else if (m_points && m_pointsMutex)
            {
                // Centre-line finalize (legacy)
                std::vector<glm::vec2> pts = TelemetryTrackBuilder::GetRawPointsSnapshot();
                { std::lock_guard<std::mutex> lk(*m_pointsMutex); *m_points = std::move(pts); }
                TrackRenderer::rebuildTrackCache(*m_points, *m_pointsMutex);
            }
            LoadRecentFiles();
            s_builder_finished_consumed = true;
        }
    }
    else
    {
        s_builder_finished_consumed = false;
    }

    // Lap timer overlay (race info) — hidden in PRO view
    if (m_ui_elements && g_race_manager && !m_proMode)
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

    // Wipe the loaded map (both classic and PRO views) when the Track Server
    // connection drops — manual disconnect or connection loss.
    {
        static bool s_srv_was_connected = false;
        bool conn = TrackServerClient::isConnected();
        if (s_srv_was_connected && !conn && g_race_manager)
            g_race_manager->ResetMap();
        s_srv_was_connected = conn;
    }

    RenderPrototypeToast();
    RenderNetworkingModal();
    AccountsPanel::Render(m_fontUI, m_fontUBold);
    RenderAutoStopModal();

    // Render help modal if open
    RenderHelpModal();

    // Review screen — rendered last so nothing draws on top of it.
    // (Draws itself only while the recorder is in the Review phase.)
    TrackReviewPanel::Render(m_fontTitle, m_fontUI, m_fontJBMonoBold);
}

void UI::RenderRaceStatusBar(ModeManager* modeManager)
{
    if (!modeManager || m_showSplash || m_proMode)
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

    // Session phase: server race state when connected (Practice/Race/Race
    // Finishing/Race Finished), local RaceManager session otherwise.
    const bool server_session = TrackServerClient::isConnected() &&
                                !TrackServerClient::raceState().empty();
    if (server_session)
        modeManager->SyncWithServerState(TrackServerClient::raceState());
    else if (g_race_manager)
        modeManager->SyncWithSessionState(g_race_manager->GetSessionState());

    // Current flag → both status-bar squares. Green is the default.
    FlagColor flag_color = FlagColor::Green;
    if (TrackServerClient::isConnected())
    {
        const std::string f = TrackServerClient::currentFlag();
        if      (f == "yellow") flag_color = FlagColor::Yellow;
        else if (f == "red")    flag_color = FlagColor::Red;
        else if (f == "finish") flag_color = FlagColor::Checkered;
    }
    m_raceDisplay.GetStatusBar().GetFlags().SetLeftFlag(flag_color);
    m_raceDisplay.GetStatusBar().GetFlags().SetRightFlag(flag_color);

    if (g_race_manager)
    {
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

    // While the track recorder is running, its phase replaces the race phase
    // (Practice/Race) in the pill, so the user always sees what is going on.
    const char* phase_label = modeManager->GetPhaseLabel();
    switch (TelemetryTrackBuilder::GetPhase())
    {
        case EdgePhase::Left:    phase_label = "REC: LEFT EDGE";  break;
        case EdgePhase::Transit: phase_label = "REC: TRANSIT";    break;
        case EdgePhase::Right:   phase_label = "REC: RIGHT EDGE"; break;
        case EdgePhase::Review:  phase_label = "TRACK REVIEW";    break;
        default: break;
    }

    ImFont* font = m_fontUI ? m_fontUI : ImGui::GetFont();
    ImGui::PushFont(font);

    const ImVec2 system_size = ImGui::CalcTextSize(system_time.c_str());
    const ImVec2 session_size = ImGui::CalcTextSize(session_time.c_str());
    const ImVec2 phase_size = ImGui::CalcTextSize(phase_label);

    // Геометрия в пунктах × DPI. Пилюля живёт в одной строке с меню и, как
    // в Blender, при нехватке места сжимается / прячет часы / исчезает —
    // но никогда не налезает на пункты меню (слева) и кнопку PRO (справа).
    const float gap         = ui_scale::points(14.f);
    const float left_limit  = m_menuRightEdgeX + gap;
    const float right_limit = (m_proButtonLeftX > 0.f ? m_proButtonLeftX
                                                      : io.DisplaySize.x - ui_scale::points(70.f)) - gap;

    const float pill_h = ui_scale::points(24.f);
    const float pill_y = (UIConfig::top_bar_px() - pill_h) * 0.5f;
    const float flag_size = pill_h * 0.6f;
    const float flag_rounding = flag_size * 0.15f;
    const float pill_pad_x = ui_scale::points(12.f);

    // Ширина: хотим широкую (как в референсе), ужимаемся до доступного места
    const float pill_min_w = 2.f * pill_pad_x + 2.f * flag_size + phase_size.x + 2.f * ui_scale::points(10.f);
    float pill_w = ui_scale::points(440.f);
    const float avail = right_limit - left_limit;
    if (pill_w > avail) pill_w = avail;
    if (pill_w < pill_min_w)
    {
        ImGui::PopFont();
        return; // места нет совсем — прячем пилюлю целиком
    }

    float pill_x = (io.DisplaySize.x - pill_w) * 0.5f;   // по центру окна
    pill_x = std::max(pill_x, left_limit);               // но не по меню
    pill_x = std::min(pill_x, right_limit - pill_w);     // и не по кнопке PRO
    const float pill_rounding = pill_h * 0.25f;

    const float flag_y = pill_y + (pill_h - flag_size) * 0.5f;
    const float left_flag_x  = pill_x + pill_pad_x;
    const float right_flag_x = pill_x + pill_w - pill_pad_x - flag_size;
    const float text_x = pill_x + (pill_w - phase_size.x) * 0.5f;

    // Часы по бокам рисуем только если они помещаются между границами
    const float system_x = pill_x - gap - system_size.x;
    const float session_x = pill_x + pill_w + gap;
    const bool show_system  = system_x >= left_limit;
    const bool show_session = session_x + session_size.x <= right_limit;

    const ImU32 pill_bg = IM_COL32(24, 24, 24, 255);
    const ImU32 pill_border = IM_COL32(160, 160, 160, 180);
    const ImU32 text_color = IM_COL32(240, 240, 240, 255);

    // Square color follows the race flag (checkered pattern for the finish).
    float fr, fg, fb, fa;
    RaceFlags::GetFlagRGBA(flag_color, fr, fg, fb, fa);
    const ImU32 flag_fill = IM_COL32(static_cast<int>(fr * 255),
                                     static_cast<int>(fg * 255),
                                     static_cast<int>(fb * 255), 255);

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    draw_list->AddRectFilled(ImVec2(pill_x, pill_y), ImVec2(pill_x + pill_w, pill_y + pill_h), pill_bg, pill_rounding);
    draw_list->AddRect(ImVec2(pill_x, pill_y), ImVec2(pill_x + pill_w, pill_y + pill_h), pill_border, pill_rounding, 0, 1.5f);

    auto drawFlagSquare = [&](float fx)
    {
        if (flag_color == FlagColor::Checkered)
        {
            // 4x4 checkered pattern
            const int   cells = 4;
            const float cell  = flag_size / cells;
            for (int r = 0; r < cells; ++r)
                for (int c = 0; c < cells; ++c)
                {
                    const ImU32 col = ((r + c) & 1) ? IM_COL32(25, 25, 25, 255)
                                                    : IM_COL32(235, 235, 235, 255);
                    draw_list->AddRectFilled(
                        ImVec2(fx + c * cell, flag_y + r * cell),
                        ImVec2(fx + (c + 1) * cell, flag_y + (r + 1) * cell), col);
                }
            draw_list->AddRect(ImVec2(fx, flag_y),
                               ImVec2(fx + flag_size, flag_y + flag_size),
                               IM_COL32(160, 160, 160, 200));
        }
        else
        {
            draw_list->AddRectFilled(ImVec2(fx, flag_y),
                                     ImVec2(fx + flag_size, flag_y + flag_size),
                                     flag_fill, flag_rounding);
        }
    };
    drawFlagSquare(left_flag_x);
    drawFlagSquare(right_flag_x);

    const float text_y_sys = pill_y + (pill_h - system_size.y) * 0.5f;
    const float text_y_ses = pill_y + (pill_h - session_size.y) * 0.5f;
    const float text_y_phase = pill_y + (pill_h - phase_size.y) * 0.5f;

    if (show_system)
        draw_list->AddText(ImVec2(system_x, text_y_sys), text_color, system_time.c_str());
    draw_list->AddText(ImVec2(text_x, text_y_phase), text_color, phase_label);
    if (show_session)
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
        if (clipboard && *clipboard)
        {
            applyTrackData(std::string(clipboard), m_points, m_pointsMutex);
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
		TelemetryTrackBuilder::StartLeftEdge();
		std::cout << "[UI] Dual-edge recording started — drive the LEFT edge.\n";
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
            applyTrackFile(m_recentFiles[i].path, m_points, m_pointsMutex);
            m_showSplash = false;
            m_closeSplash = true;
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

void UI::RenderProView()
{
    ProContext ctx{
        m_fontRegular,
        m_fontUBold,
        m_fontTitle,
        m_fontRobotoMono,
        m_fontRussoSmall,
        m_fontJBMonoBold,
        m_logoTexture,
        { m_numIcons[0], m_numIcons[1], m_numIcons[2], m_numIcons[3], m_numIcons[4],
          m_numIcons[5], m_numIcons[6], m_numIcons[7], m_numIcons[8] }
    };
    Pro::Render(ctx, m_swipeAnim);
}

// ── Legacy RenderProView body removed — logic lives in src/ui/pro/
#if 0  // kept for reference only, never compiled
static void _legacy_pro_view()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 sz = viewport->Size;
    const float topH    = UIConfig::TOP_MENU_HEIGHT    * sz.y;
    const float bottomH = UIConfig::BOTTOM_MENU_HEIGHT * sz.y;
    const float contentH = sz.y - topH - bottomH;

    // Slides in from the LEFT: anim=0 → panel is off-screen left (-sz.x), anim=1 → fully on screen (0)
    const float panelX = -sz.x * (1.f - m_swipeAnim);

    ImGui::SetNextWindowPos(ImVec2(panelX, topH));
    ImGui::SetNextWindowSize(ImVec2(sz.x, contentH));
    ImGui::SetNextWindowBgAlpha(1.f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(28.f, 22.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(12.f, 10.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 14, 20, 255));

    ImGui::Begin("##ProView", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollWithMouse);

    // ── Title row ────────────────────────────────────────────────────────────
    if (m_logoTexture) {
        const float iconSz = 28.f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
        ImGui::Image((ImTextureID)(intptr_t)m_logoTexture, ImVec2(iconSz, iconSz));
        ImGui::SameLine(0, 10.f);
    }

    ImGui::PushFont(m_fontTitle ? m_fontTitle : m_fontUI);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xDA, 0xA5, 0x40, 255));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    ImGui::TextUnformatted("PRO");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine(0, 12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(130, 130, 145, 255));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (m_fontTitle ? m_fontTitle->FontSize * 0.35f : 6.f));
    ImGui::TextUnformatted("Advanced analytics dashboard");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    // Gold separator line
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + sz.x - 56.f, p.y),
                    IM_COL32(0xDA, 0xA5, 0x40, 80), 1.f);
        ImGui::Dummy(ImVec2(0, 6.f));
    }

    // ── Feature cards ────────────────────────────────────────────────────────
    struct Card { const char* label; const char* desc; };
    static constexpr Card cards[] = {
        { "Trajectory",      "Racing line vs ideal line, braking & throttle zones." },
        { "Sectors",         "Mini-sector timing, per-corner time-loss heatmap."     },
        { "Driver Errors",   "Auto-detect lock-ups, oversteer, missed apexes."       },
        { "Replay",          "Full lap replay with telemetry overlay & comparison."  },
        { "Live Telemetry",  "G-force, speed trace, steering angle vs. position."    },
        { "Leaderboard+",   "Per-sector ranking, consistency score, trend charts."  },
    };
    constexpr int COLS = 3;
    constexpr int N    = 6;

    const float gap  = 12.f;
    const float padX = 28.f * 2.f;
    const float cardW = (sz.x - padX - gap * (COLS - 1)) / COLS;
    const float cardH = (contentH - 120.f) / 2.f;

    for (int i = 0; i < N; ++i) {
        if (i % COLS != 0) ImGui::SameLine(0, gap);
        ImGui::PushID(i);

        ImGui::PushStyleColor(ImGuiCol_ChildBg,      IM_COL32(24, 24, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_Border,       IM_COL32(0xDA, 0xA5, 0x40, 35));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f, 14.f));

        ImGui::BeginChild("##card", ImVec2(cardW, cardH), true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Card header — gold dot + label
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 dotPos = ImGui::GetCursorScreenPos();
        dotPos.x += 3.f; dotPos.y += 9.f;
        dl->AddCircleFilled(dotPos, 4.f, IM_COL32(0xDA, 0xA5, 0x40, 255));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.f);

        ImGui::PushFont(m_fontUBold ? m_fontUBold : m_fontUI);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 230, 235, 255));
        ImGui::TextUnformatted(cards[i].label);
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Spacing();

        // Description
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 140, 155, 255));
        ImGui::PushTextWrapPos(cardW - 16.f);
        ImGui::TextUnformatted(cards[i].desc);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        // "Coming soon" badge at bottom-right of card
        const float badgeY = cardH - 28.f;
        ImGui::SetCursorPosY(badgeY);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xDA, 0xA5, 0x40, 120));
        float tw = ImGui::CalcTextSize("Coming soon").x;
        ImGui::SetCursorPosX(cardW - tw - 16.f);
        ImGui::TextUnformatted("Coming soon");
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopID();

        if (i == COLS - 1) ImGui::Spacing(); // row gap
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}
#endif  // legacy PRO view — see src/ui/pro/ for current implementation

void UI::RenderTopMenu()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 display_size = viewport->Size;
    
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, UIConfig::top_bar_px()));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    // === TOP MENU BAR STYLING ===
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    // Паддинги меню в пунктах × DPI, а не в долях окна: пункты меню держат
    // физический размер и не разъезжаются при малом/большом окне.
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(ui_scale::points(8.0f), ui_scale::points(6.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(
        UIConfig::MENU_ITEM_SPACING,
        0
    ));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
        ImVec2(ui_scale::points(8.0f), ui_scale::points(4.0f)));


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
        
        // Logo + PRO badge live in the Windows title bar (glfwSetWindowIcon /
        // glfwSetWindowTitle) — the navbar only gets the left padding.
        ImGui::SetCursorPosX(ui_scale::points(10.0f));
        
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
                ofn.lpstrFilter = "trk2\0*.trk2\0Track TXT\0*.txt\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                std::string savesPath = "src/saves";
                ofn.lpstrInitialDir = savesPath.c_str();
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                if (GetOpenFileNameA(&ofn))
                {
                    applyTrackFile(ofn.lpstrFile, m_points, m_pointsMutex);
                    m_showSplash = false;
                    m_closeSplash = true;
                }
            }

            ImGui::Separator();

            {
                // Same phase flow as the on-screen HUD button.
                const EdgePhase phase  = TelemetryTrackBuilder::GetPhase();
                const bool inProgress = TelemetryTrackBuilder::IsActive() || phase == EdgePhase::Review;
                const char* label =
                    (phase == EdgePhase::Left)    ? "Track Recording: LEFT edge"  :
                    (phase == EdgePhase::Transit) ? "Track Recording: transit"    :
                    (phase == EdgePhase::Right)   ? "Track Recording: RIGHT edge" :
                    (phase == EdgePhase::Review)  ? "Track Recording: review"     :
                    inProgress                    ? "Track Recording: ON" : "Track Recording: OFF";
                if (ImGui::MenuItem(label))
                {
                    if (inProgress) { TelemetryTrackBuilder::Stop(); }
                    else            { TelemetryTrackBuilder::StartLeftEdge(); }
                }
                if (phase == EdgePhase::Left && ImGui::MenuItem("  Finish Left Edge"))
                    TelemetryTrackBuilder::FinishLeftEdge();
                if (phase == EdgePhase::Transit && ImGui::MenuItem("  Start Right Edge"))
                    TelemetryTrackBuilder::StartRightEdge();
                if (phase == EdgePhase::Right && ImGui::MenuItem("  Finish & Review"))
                    TelemetryTrackBuilder::FinishRightEdge();
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
                    p.gForceX = 1,5;
                    p.gForceY = 2;

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

            {
                static std::atomic<bool> s_dual_sim_active{false};
                const bool simOn = s_dual_sim_active.load();
                if (ImGui::MenuItem("Test: Simulate Dual-Edge Track", nullptr, simOn))
                {
                    if (simOn) {
                        TelemetryTrackBuilder::Stop();
                        s_dual_sim_active.store(false);
                    } else {
                        // Пониженный порог чтобы Switch стал доступен быстро
                        TelemetryTrackBuilder::Settings s;
                        s.minPointsToClose = 50;
                        TelemetryTrackBuilder::StartLeftEdge(s);
                        s_dual_sim_active.store(true);

                        std::thread([]() {
                            const double baseLat = 37.4219999, baseLon = -122.0840575;
                            double e0, n0; int zone; bool northp;
                            GeographicLib::UTMUPS::Forward(baseLat, baseLon, zone, northp, e0, n0);

                            TelemetryPacket p{};
                            p.MagicMarker = PACKET_MAGIC_DATA;
                            p.ID = 4242; p.fixtype = 4; p.speed = 6000;
                            uint32_t t = 0;

                            auto sendEllipse = [&](double rx, double ry, int steps) {
                                for (int i = 0; i <= steps; ++i) {
                                    if (!s_dual_sim_active.load()) return;
                                    double a = (double)i / steps * SimulationConstants::TWO_PI;
                                    double lat = 0, lon = 0;
                                    GeographicLib::UTMUPS::Reverse(zone, northp,
                                        e0 + std::cos(a) * rx, n0 + std::sin(a) * ry, lat, lon);
                                    p.lat = (int32_t)(lat * 1e7);
                                    p.lon = (int32_t)(lon * 1e7);
                                    p.time = t; t += 8;
                                    processIncomingTelemetry(p);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(8));
                                }
                            };

                            sendEllipse(55.0, 40.0, 400);  // левая грань

                            // Ждём пока пользователь пройдёт Transit:
                            // "Finish Left Edge" → "Start Right Edge"
                            while (s_dual_sim_active.load() &&
                                   TelemetryTrackBuilder::GetPhase() != EdgePhase::Right)
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                            sendEllipse(70.0, 55.0, 400);  // правая грань

                            s_dual_sim_active.store(false);
                        }).detach();
                    }
                }
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
            // Пользовательский множитель поверх автоматического DPI-масштаба;
            // сохраняется между запусками (ui_scale.ini).
            if (ImGui::BeginMenu("UI Scale"))
            {
                static constexpr float scale_options[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
                static constexpr const char* scale_labels[] = { "75%", "100% (Auto)", "125%", "150%", "175%", "200%" };
                const float current = ui_scale::user_scale();
                for (int i = 0; i < 6; ++i)
                    if (ImGui::MenuItem(scale_labels[i], nullptr,
                                        fabsf(current - scale_options[i]) < 0.01f))
                        ui_scale::set_user_scale(scale_options[i]);
                ImGui::EndMenu();
            }
            ImGui::Separator();
           ImGui::MenuItem("Prototype panel", nullptr, &m_allowPrototypeToast);
            ImGui::MenuItem("Vehicle names", nullptr, &g_show_vehicle_names);
            if (ImGui::MenuItem("Toggle Fullscreen", "F11", false, false)) {}
            if (m_proMode) {
                ImGui::Separator();
                if (ImGui::MenuItem("Lock PRO Layout", nullptr, Pro::g_pro_layout_locked))
                    Pro::g_pro_layout_locked = !Pro::g_pro_layout_locked;
            }
            ImGui::EndMenu();
        }

        RenderRaceMenu();
        
        if (ImGui::BeginMenu("Networking"))
        {
            if (ImGui::MenuItem("Connect to Server", "Shift+C"))
            {
                UpdateNetworkingIps();
                m_networkingModalMode = NetworkingModalMode::Connect;
                m_show_networking_modal = true;
                m_networking_addr_invalid = false;
                m_networking_password_invalid = false;
                if (m_networking_addr[0] == 0)
                {
                    if (!m_external_ip.empty())
                        snprintf(m_networking_addr, sizeof(m_networking_addr), "%s:%u", m_external_ip.c_str(), (unsigned)m_display_port);
                }
            }
            if (ImGui::MenuItem("Disconnect"))
            {
                TrackServerClient::stop();
            }
            // Accounts management — admins only (create timed access tokens).
            if (TrackServerClient::isConnected() && TrackServerClient::role() == "admin")
            {
                if (ImGui::MenuItem("Accounts"))
                    AccountsPanel::Toggle();
            }
            ImGui::Separator();
            {
                ImGui::BeginDisabled();
                if (TrackServerClient::isConnected())
                    ImGui::MenuItem((std::string("Track Server: Connected (") + TrackServerClient::role() + ")").c_str(), nullptr, false, false);
                else if (TrackServerClient::isRunning())
                    ImGui::MenuItem("Track Server: Connecting...", nullptr, false, false);
                else
                    ImGui::MenuItem("Track Server: Not connected", nullptr, false, false);

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

        // Конец пунктов меню — левая граница для пилюли статуса (см.
        // RenderRaceStatusBar): она не имеет права налезать на меню.
        m_menuRightEdgeX = ImGui::GetCursorScreenPos().x;

        // === PRO / LITE TOGGLE BUTTON (right side of navbar) ===
        {
            const float barH    = ImGui::GetWindowHeight();
            const float btnH    = barH - ui_scale::points(8.f);
            const float btnW    = ui_scale::points(52.f);
            const float marginR = ui_scale::points(10.f);
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btnW - marginR);
            ImGui::SetCursorPosY((barH - btnH) * 0.5f);
            // Левая граница кнопки — правый предел для пилюли/таймеров
            m_proButtonLeftX = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - btnW - marginR;

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
            if (m_proMode) {
                ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(55,  55,  60, 220));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(75,  75,  80, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(40,  40,  45, 255));
                ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(210, 210, 220, 255));
                if (ImGui::Button("Lite", ImVec2(btnW, btnH))) {
                    m_proMode = false;
                    glfwSetWindowTitle(m_window, UIConfig::APP_NAME);
                }
                ImGui::PopStyleColor(4);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0xDA, 0xA5, 0x40, 190));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0xDA, 0xA5, 0x40, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0xB8, 0x86, 0x20, 255));
                ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(255, 255, 255, 255));
                if (ImGui::Button("PRO", ImVec2(btnW, btnH))) {
                    m_proMode = true;
                    glfwSetWindowTitle(m_window, "RAJAGP PRO");
                }
                ImGui::PopStyleColor(4);
            }
            ImGui::PopStyleVar(); // FrameRounding
        }

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
    
    float bottom_height = UIConfig::bottom_bar_px();
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

    // Telemetry packets per second on the left; when connected to the Track
    // Server, also frame loss % and delay (computed from seq/server_time_ms).
    {
        const uint32_t pps = telemetryGetPacketsPerSecond();
        char pps_text[96];
        if (TrackServerClient::isConnected()) {
            const float loss  = TrackServerClient::netLossPercent();
            const int   delay = TrackServerClient::netDelayMs();
            snprintf(pps_text, sizeof(pps_text),
                     "PPS: %u   Loss: %.1f%%   Delay: %d ms",
                     (unsigned)pps, loss, delay);
        } else {
            snprintf(pps_text, sizeof(pps_text), "PPS: %u", (unsigned)pps);
        }
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
        bindRow("Ctrl+S",         "Save race results as .txt (dialog)");
        bindRow("Ctrl+P",         "Print race results");
        bindRow("Space",          "Finalize open track recording");

        // ── NETWORK ───────────────────────────────────────────────────────
        sectionHeader("Network");
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

    ImGuiIO& io      = ImGui::GetIO();
    const ImVec2 dsz = io.DisplaySize;

    // Adaptive: width clamped, height derived from content
    // Размеры в пунктах × DPI (ui_scale) — см. комментарий в NetworkingModal
    float mW = ui_scale::points(420.0f);
    if (mW > dsz.x * 0.9f) mW = dsz.x * 0.9f;
    const float titleH = ui_scale::points(44.0f);
    const float padX   = mW * 0.08f;
    const float padY   = ui_scale::points(24.0f);
    const float fieldW = mW - padX * 2;
    const char* subTxt = "Choose when the race will automatically end:";
    ImFont* bodyFont   = m_fontUI ? m_fontUI : ImGui::GetFont();
    ImGui::PushFont(bodyFont);
    const float subH   = ImGui::CalcTextSize(subTxt, nullptr, false, fieldW).y + ui_scale::points(24.0f);
    const float labelH = ImGui::GetFontSize() + ui_scale::points(14.0f);
    ImGui::PopFont();
    const float fieldH = ui_scale::points(34.0f);
    const float gap    = ui_scale::points(15.0f);
    const float btnH   = ui_scale::points(40.0f);
    const float mH     = titleH + padY + subH + (labelH + fieldH) * 2 + gap + padY + btnH + padY;
    const ImVec2 mPos((dsz.x - mW) * 0.5f, (dsz.y - mH) * 0.5f);
    const ImVec2 mEnd(mPos.x + mW, mPos.y + mH);

    // ── colors (Help modal palette) ─────────────────────────────────────────
    const ImVec4 colGold(218.f/255.f, 165.f/255.f, 64.f/255.f, 1.f);
    const ImU32  uGold  = IM_COL32(218, 165, 64, 255);
    const ImU32  uSep   = IM_COL32(55, 55, 55, 255);
    const ImU32  uFrame = IM_COL32(0xFF, 0xFF, 0xFF, (int)(255 * 0.21f));

    // ── overlay (blocks world input, click outside closes) ──────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(dsz);
    ImGui::SetNextWindowBgAlpha(UIConfig::MODAL_OVERLAY_ALPHA);
    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, UIConfig::MODAL_OVERLAY_ALPHA));
    if (ImGui::Begin("##AutoStopOverlay", nullptr, bg_flags))
    {
        ImVec2 mp = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(0) &&
            (mp.x < mPos.x || mp.x > mEnd.x || mp.y < mPos.y || mp.y > mEnd.y))
            g_show_autostop_modal = false;
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (!g_show_autostop_modal) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { g_show_autostop_modal = false; return; }

    // ── modal window (Help style) ────────────────────────────────────────────
    ImGui::SetNextWindowPos(mPos);
    ImGui::SetNextWindowSize(ImVec2(mW, mH));
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(UIConfig::MODAL_BG_R, UIConfig::MODAL_BG_G, UIConfig::MODAL_BG_B, UIConfig::MODAL_BG_ALPHA));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.22f, 0.22f, 0.22f, 1.f));

    bool modal_open = true;
    if (ImGui::Begin("##AutoStopModal", &modal_open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Custom title bar (Help style) ──────────────────────────────────
        const float titleBarBot = mPos.y + titleH;
        dl->AddRectFilled(mPos, ImVec2(mEnd.x, titleBarBot),
            IM_COL32((int)(UIConfig::MODAL_TITLE_BG_R * 255),
                     (int)(UIConfig::MODAL_TITLE_BG_G * 255),
                     (int)(UIConfig::MODAL_TITLE_BG_B * 255), 255),
            10.0f, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(mPos.x, titleBarBot), ImVec2(mEnd.x, titleBarBot), uSep, 1.0f);
        dl->AddRectFilled(ImVec2(mPos.x, mPos.y + 3.f), ImVec2(mPos.x + 4.0f, titleBarBot - 3.f), uGold, 2.0f);
        if (m_fontUBold) ImGui::PushFont(m_fontUBold);
        {
            const char* tTxt = "Auto Stop Conditions";
            const ImVec2 tSz = ImGui::CalcTextSize(tTxt);
            dl->AddText(ImVec2(mPos.x + 14.f, mPos.y + (titleH - tSz.y) * 0.5f),
                        IM_COL32(235, 235, 235, 255), tTxt);
        }
        if (m_fontUBold) ImGui::PopFont();

        auto drawInputFrame = [&]() {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                uFrame, 0.0f, 0, 1.5f);
        };

        bool doStart = false;
        ImGui::PushFont(bodyFont);

        float y = titleH + padY;
        ImGui::SetCursorPos(ImVec2(padX, y));
        ImGui::PushTextWrapPos(padX + fieldW);
        ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.f), "%s", subTxt);
        ImGui::PopTextWrapPos();
        y += subH;

        // Inactive condition (0 laps / 0 time) is grayed so the user sees what applies
        int th = 0, tm = 0, ts = 0;
        int previewSeconds = 0;
        if (sscanf_s(m_autostop_time, "%d:%d:%d", &th, &tm, &ts) == 3)
            previewSeconds = th * 3600 + tm * 60 + ts;
        else if (sscanf_s(m_autostop_time, "%d:%d", &tm, &ts) == 2)
            previewSeconds = tm * 60 + ts;
        const bool lapsActive = m_autostop_laps > 0;
        const bool timeActive = previewSeconds > 0;
        const ImVec4 colGray(0.45f, 0.45f, 0.45f, 1.f);

        // Shared input styling
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f, 0.13f, 0.13f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, (fieldH - ImGui::GetFontSize()) * 0.5f));

        ImGui::SetCursorPos(ImVec2(padX, y));
        ImGui::TextColored(lapsActive ? colGold : colGray, "Laps");
        ImGui::SetCursorPos(ImVec2(padX, y + labelH));
        ImGui::SetNextItemWidth(fieldW);
        ImGui::PushStyleColor(ImGuiCol_Text, lapsActive ? ImVec4(0.94f, 0.94f, 0.94f, 1.f) : colGray);
        doStart |= ImGui::InputInt("##laps", &m_autostop_laps, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        drawInputFrame();
        y += labelH + fieldH + gap;

        ImGui::SetCursorPos(ImVec2(padX, y));
        ImGui::TextColored(timeActive ? colGold : colGray, "Time (HH:MM:SS)");
        ImGui::SetCursorPos(ImVec2(padX, y + labelH));
        ImGui::SetNextItemWidth(fieldW);
        ImGui::PushStyleColor(ImGuiCol_Text, timeActive ? ImVec4(0.94f, 0.94f, 0.94f, 1.f) : colGray);
        doStart |= ImGui::InputText("##time", m_autostop_time, sizeof(m_autostop_time), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        drawInputFrame();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // Start Race button (Help-style gold)
        if (m_fontUBold) ImGui::PushFont(m_fontUBold);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        colGold);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(238.f/255.f, 185.f/255.f, 84.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(198.f/255.f, 145.f/255.f, 44.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.07f, 0.07f, 0.07f, 1.f));
        ImGui::SetCursorPos(ImVec2(padX, mH - padY - btnH));
        doStart |= ImGui::Button("Start Race", ImVec2(fieldW, btnH));
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        if (m_fontUBold) ImGui::PopFont();

        if (doStart)
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

            // Connected as admin → auto-stop and start run on the Track Server
            // (laps counted on the leader, classification at the line).
            if (TrackServerClient::isConnected() && TrackServerClient::role() == "admin")
            {
                char cmd[128];
                snprintf(cmd, sizeof(cmd),
                         R"({"type":"auto_stop","laps":%d,"seconds":%d})",
                         m_autostop_laps, static_cast<int>(totalSeconds));
                TrackServerClient::sendCommand(cmd);
                TrackServerClient::sendCommand(R"({"type":"race","action":"start"})");
            }
            else if (g_race_manager)
            {
                g_race_manager->SetAutoStopConditions(m_autostop_laps, totalSeconds);
                g_race_manager->StartSession();
            }
            g_show_autostop_modal = false;
        }

        ImGui::PopFont();
    }
    ImGui::End();

    if (!modal_open)
        g_show_autostop_modal = false;

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
