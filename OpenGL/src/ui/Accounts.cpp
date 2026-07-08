#include "Accounts.h"

#include <cstring>
#include <string>
#include <vector>

#include <imgui/imgui.h>

#include "UI_Config.h"
#include "../network/TrackServerClient.h"

namespace AccountsPanel {
namespace {

bool s_open = false;
bool s_focus_pending = false; // focus the modal ONCE on open — re-focusing
                              // every frame would close combo popups instantly
bool s_refreshed_once = false;
char s_user_name[64] = "";
int  s_duration_idx = 1; // default: 1 day

const char* kDurationLabels[] = { "1 hour", "1 day", "Unlimited" };
const char* kDurationValues[] = { "1h", "1d", "unlimited" };

} // namespace

void Toggle()
{
    s_open = !s_open;
    if (s_open)
        s_focus_pending = true;
}

// Help-modal twin: full-screen dark overlay + centered fixed-proportion modal
// (same UIConfig::HELP_MODAL_* fractions of the display, so it adapts to any
// monitor exactly like the Help window), custom gold-accented title bar,
// scrollable content, gold Close button at the bottom.
void Render(ImFont* bodyFont, ImFont* boldFont)
{
    if (!s_open)
        return;

    ImGuiIO& io      = ImGui::GetIO();
    const ImVec2 dsz = io.DisplaySize;
    const float  mW  = UIConfig::HELP_MODAL_WIDTH  * dsz.x;
    const float  mH  = UIConfig::HELP_MODAL_HEIGHT * dsz.y;
    const ImVec2 mPos((dsz.x - mW) * 0.5f, (dsz.y - mH) * 0.5f);
    const ImVec2 mEnd(mPos.x + mW, mPos.y + mH);

    // ── colors (identical to the Help modal) ─────────────────────────────────
    const ImVec4 colGold (218.f/255.f, 165.f/255.f, 64.f/255.f, 1.f);
    const ImVec4 colDim  (0.70f, 0.70f, 0.70f, 1.f);
    const ImU32  uGold   = IM_COL32(218, 165, 64, 255);
    const ImU32  uSep    = IM_COL32(55,  55,  55, 255);

    // ── overlay (blocks world input; click outside closes) ──────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(dsz);
    ImGui::SetNextWindowBgAlpha(UIConfig::MODAL_OVERLAY_ALPHA);
    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, UIConfig::MODAL_OVERLAY_ALPHA));
    if (ImGui::Begin("##AccountsOverlay", nullptr, bg_flags))
    {
        const ImVec2 mouse_pos = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(0) &&
            (mouse_pos.x < mPos.x || mouse_pos.x > mEnd.x ||
             mouse_pos.y < mPos.y || mouse_pos.y > mEnd.y))
        {
            s_open = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (!s_open) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { s_open = false; return; }

    // ── modal window ─────────────────────────────────────────────────────────
    const float padX   = mW * 0.044f;
    const float padY   = mH * 0.03f;
    const float titleH = mH * 0.11f;

    ImGui::SetNextWindowPos(mPos);
    ImGui::SetNextWindowSize(ImVec2(mW, mH));
    if (s_focus_pending) {
        ImGui::SetNextWindowFocus();
        s_focus_pending = false;
    }
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
    if (ImGui::Begin("##AccountsModal", &modal_open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Custom title bar ─────────────────────────────────────────────────
        const float titleBarBot = mPos.y + titleH;
        dl->AddRectFilled(mPos, ImVec2(mEnd.x, titleBarBot),
            IM_COL32((int)(UIConfig::MODAL_TITLE_BG_R*255),
                     (int)(UIConfig::MODAL_TITLE_BG_G*255),
                     (int)(UIConfig::MODAL_TITLE_BG_B*255), 255),
            10.0f, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(mPos.x, titleBarBot), ImVec2(mEnd.x, titleBarBot), uSep, 1.0f);
        dl->AddRectFilled(ImVec2(mPos.x,        mPos.y + 3.f),
                          ImVec2(mPos.x + 4.0f, titleBarBot - 3.f),
                          uGold, 2.0f);
        if (boldFont) ImGui::PushFont(boldFont);
        {
            const char* tTxt = "Accounts";
            const ImVec2 tSz = ImGui::CalcTextSize(tTxt);
            dl->AddText(ImVec2(mPos.x + padX + 8.f, mPos.y + (titleH - tSz.y) * 0.5f),
                        IM_COL32(235, 235, 235, 255), tTxt);
        }
        if (boldFont) ImGui::PopFont();
        if (bodyFont) ImGui::PushFont(bodyFont);
        {
            const char* vTxt = UIConfig::APP_VERSION;
            const ImVec2 vSz = ImGui::CalcTextSize(vTxt);
            dl->AddText(ImVec2(mEnd.x - vSz.x - padX * 0.6f, mPos.y + (titleH - vSz.y) * 0.5f),
                        uGold, vTxt);
        }
        if (bodyFont) ImGui::PopFont();

        // ── Scrollable content ───────────────────────────────────────────────
        const float btnH     = mH * 0.062f;
        const float contentH = mH - titleH - 3.f * padY - btnH;
        ImGui::SetCursorPos(ImVec2(padX, titleH + padY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 6.f));
        ImGui::BeginChild("##AccountsContent", ImVec2(mW - padX * 2, contentH), false);

        ImFont* body = bodyFont ? bodyFont : ImGui::GetFont();
        ImFont* bold = boldFont ? boldFont : body;

        auto sectionHeader = [&](const char* label)
        {
            ImGui::Dummy(ImVec2(0, 6.f));
            ImGui::PushFont(bold);
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

        ImGui::PushFont(body);

        const bool is_admin = TrackServerClient::isConnected() &&
                              TrackServerClient::role() == "admin";
        if (!is_admin)
        {
            ImGui::Dummy(ImVec2(0, 8.f));
            ImGui::TextWrapped("Accounts are managed by the race admin.");
            ImGui::TextColored(colDim, "Connect to the Track Server with the admin "
                               "token (Networking -> Connect to Server).");
            s_refreshed_once = false;
        }
        else
        {
            if (!s_refreshed_once) {
                s_refreshed_once = true;
                TrackServerClient::sendCommand(R"({"type":"list_users"})");
            }

            // ---- Create user ------------------------------------------------
            sectionHeader("Create user access");

            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.13f, 0.13f, 0.13f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
            ImGui::SetNextItemWidth(mW * 0.32f);
            ImGui::InputTextWithHint("##AccName", "user name", s_user_name,
                                     sizeof(s_user_name));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(mW * 0.20f);
            ImGui::Combo("##AccDuration", &s_duration_idx, kDurationLabels,
                         IM_ARRAYSIZE(kDurationLabels));
            ImGui::PopStyleColor(2);
            ImGui::SameLine();

            ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, 0.f);
            ImGui::PushStyleColor(ImGuiCol_Button,        colGold);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(238.f/255.f, 185.f/255.f, 84.f/255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(198.f/255.f, 145.f/255.f, 44.f/255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.07f, 0.07f, 0.07f, 1.f));
            ImGui::PushFont(bold);
            const bool doCreate = ImGui::Button("Create") && s_user_name[0] != 0;
            ImGui::PopFont();
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();

            if (doCreate) {
                TrackServerClient::sendCommand(
                    std::string(R"({"type":"create_user","name":")") + s_user_name +
                    R"(","duration":")" + kDurationValues[s_duration_idx] + R"("})");
                TrackServerClient::sendCommand(R"({"type":"list_users"})");
            }
            ImGui::TextColored(colDim, "Token appears in the log below - click a line to copy.");

            // ---- Existing users ---------------------------------------------
            sectionHeader("Users");
            if (ImGui::SmallButton("Refresh"))
                TrackServerClient::sendCommand(R"({"type":"list_users"})");

            const auto users = TrackServerClient::userList();
            if (users.empty()) {
                ImGui::TextColored(colDim, "(no users yet)");
            } else {
                ImGui::PushStyleColor(ImGuiCol_TableBorderLight,  ImVec4(0.20f, 0.20f, 0.20f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.30f, 0.30f, 0.30f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_TableRowBg,        ImVec4(0.10f, 0.10f, 0.10f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,     ImVec4(0.13f, 0.13f, 0.13f, 1.f));
                if (ImGui::BeginTable("AccUsers", 4,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch, 2.f);
                    ImGui::TableSetupColumn("Token",   ImGuiTableColumnFlags_WidthStretch, 2.f);
                    ImGui::TableSetupColumn("Expires", ImGuiTableColumnFlags_WidthStretch, 3.f);
                    ImGui::TableSetupColumn("##act",   ImGuiTableColumnFlags_WidthFixed,   mW * 0.14f);
                    ImGui::TableHeadersRow();
                    for (const auto& u : users) {
                        ImGui::TableNextRow();
                        ImGui::PushID(u.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(u.name.c_str());
                        ImGui::TableNextColumn();
                        // Token comes straight from the server user list, so it
                        // survives app restarts — click to copy.
                        if (!u.token.empty()) {
                            ImGui::PushStyleColor(ImGuiCol_Text, colGold);
                            if (ImGui::Selectable(u.token.c_str(), false,
                                                  ImGuiSelectableFlags_None))
                                ImGui::SetClipboardText(u.token.c_str());
                            ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Click to copy token");
                        } else {
                            ImGui::TextColored(colDim, "-");
                        }
                        ImGui::TableNextColumn();
                        ImGui::TextColored(colDim, "%s", u.expires.c_str());
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton("Revoke")) {
                            TrackServerClient::sendCommand(
                                std::string(R"({"type":"revoke_user","name":")") + u.name + R"("})");
                            TrackServerClient::sendCommand(R"({"type":"list_users"})");
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleColor(4);
            }

            // ---- Server responses (tokens etc.) -----------------------------
            sectionHeader("Log (click line to copy)");
            if (ImGui::SmallButton("Clear##log"))
                TrackServerClient::clearResponses();
            for (const auto& line : TrackServerClient::adminResponses()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.80f, 0.80f, 1.f));
                if (ImGui::Selectable(line.c_str()))
                    ImGui::SetClipboardText(line.c_str());
                ImGui::PopStyleColor();
            }
        }

        ImGui::PopFont(); // body

        ImGui::EndChild();
        ImGui::PopStyleVar(); // ItemSpacing

        // ── Close button — anchored exactly padY above modal bottom ─────────
        const float btnW = mW * 0.22f;
        ImGui::SetCursorPos(ImVec2((mW - btnW) * 0.5f, mH - padY - btnH));
        ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        colGold);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(238.f/255.f, 185.f/255.f, 84.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(198.f/255.f, 145.f/255.f, 44.f/255.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.07f, 0.07f, 0.07f, 1.f));
        if (boldFont) ImGui::PushFont(boldFont);
        if (ImGui::Button("Close", ImVec2(btnW, btnH)))
            s_open = false;
        if (boldFont) ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }
    ImGui::End();

    if (!modal_open) s_open = false;

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

} // namespace AccountsPanel
