#pragma once
// ============================================================================
// UI FOR RACE DATA CHANGES - LAP TIMES, LEADERBOARD, START/FINISH CROSSING
// ============================================================================

#include "../../racing/RaceManager.h"
#include "../../network/TrackServerClient.h"
#include "../../../libraries/include/imgui/imgui.h"
#include <iostream>
#include <string>

extern RaceManager* g_race_manager;

extern bool g_show_autostop_modal;

void RenderRaceMenu()
{
    if (ImGui::BeginMenu("Race"))
    {
        SessionState state = SessionState::Idle;
        if (g_race_manager)
            state = g_race_manager->GetSessionState();

        // Connected as admin → race control runs on the Track Server (the
        // server computes laps/positions there); otherwise the local
        // RaceManager session is used, exactly as before.
        const bool server_admin = TrackServerClient::isConnected() &&
                                  TrackServerClient::role() == "admin";
        const std::string srv_state = server_admin ? TrackServerClient::raceState()
                                                   : std::string();

        const bool has_manager = (g_race_manager != nullptr);
        const bool has_started = has_manager && state != SessionState::Idle;
        const bool can_start = server_admin
            ? (srv_state != "running")
            : (has_manager && !has_started);
        const bool can_end = server_admin
            ? (srv_state == "running")
            : (has_manager && (state == SessionState::Active || state == SessionState::Finishing));
        const bool can_restart = server_admin ? true : has_started;

        if (ImGui::MenuItem("Start Race", nullptr, false, can_start))
        {
            std::cout << "[UI] Race Started\n";
            if (server_admin)
                TrackServerClient::sendCommand(R"({"type":"race","action":"start"})");
            else
                g_race_manager->StartSession();
        }

        if (ImGui::MenuItem("End Race", nullptr, false, can_end))
        {
            std::cout << "[UI] Race Ended\n";
            if (server_admin)
                TrackServerClient::sendCommand(R"({"type":"race","action":"stop"})");
            else
                g_race_manager->StopSession();
        }

        if (ImGui::MenuItem("Restart Race", nullptr, false, can_restart))
        {
            std::cout << "[UI] Race Restarted\n";
            if (server_admin)
                TrackServerClient::sendCommand(R"({"type":"race","action":"reset"})");
            else
                g_race_manager->ResetSession();
        }

        if (server_admin && !srv_state.empty())
        {
            ImGui::BeginDisabled();
            ImGui::MenuItem(("Server race: " + srv_state).c_str(), nullptr, false, false);
            ImGui::EndDisabled();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Auto Stop..."))
        {
            g_show_autostop_modal = true;
        }

        ImGui::Separator();

        // ── Race control on the Track Server (admin connection only) ────────
        {
            const bool is_admin = TrackServerClient::isConnected() &&
                                  TrackServerClient::role() == "admin";
            if (ImGui::BeginMenu("Race Flags", is_admin))
            {
                auto sendFlag = [](const char* label, const char* value) {
                    if (ImGui::MenuItem(label))
                        TrackServerClient::sendCommand(
                            std::string(R"({"type":"flag","value":")") + value + R"("})");
                };
                sendFlag("Green",  "green");
                sendFlag("Yellow", "yellow");
                sendFlag("Red",    "red");
                sendFlag("Finish", "finish");
                ImGui::Separator();
                sendFlag("Clear",  "none");
                ImGui::EndMenu();
            }
            if (TrackServerClient::isConnected())
            {
                ImGui::BeginDisabled();
                ImGui::MenuItem(("Flag: " + TrackServerClient::currentFlag()).c_str(),
                                nullptr, false, false);
                ImGui::EndDisabled();
            }
            else
            {
                ImGui::BeginDisabled();
                ImGui::MenuItem("Race Flags: connect to Track Server", nullptr, false, false);
                ImGui::EndDisabled();
            }
        }

        ImGui::Separator();

        ImGui::BeginDisabled();
        ImGui::MenuItem("Settings");
        ImGui::EndDisabled();

        ImGui::EndMenu();
    }
}
