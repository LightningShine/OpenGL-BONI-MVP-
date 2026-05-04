#pragma once
// ============================================================================
// UI FOR RACE DATA CHANGES - LAP TIMES, LEADERBOARD, START/FINISH CROSSING
// ============================================================================

#include "../../racing/RaceManager.h"
#include "../../../libraries/include/imgui/imgui.h"
#include <iostream>

extern RaceManager* g_race_manager;

void RenderRaceMenu()
{
    if (ImGui::BeginMenu("Race"))
    {
        SessionState state = SessionState::Idle;
        if (g_race_manager)
            state = g_race_manager->GetSessionState();

        const bool has_manager = (g_race_manager != nullptr);
        const bool has_started = has_manager && state != SessionState::Idle;
        const bool can_start = has_manager && !has_started;
        const bool can_end = has_manager && (state == SessionState::Active || state == SessionState::Finishing);
        const bool can_restart = has_started;

        if (ImGui::MenuItem("Start Race", nullptr, false, can_start))
        {
            std::cout << "[UI] Race Started\n";
            g_race_manager->StartSession();
        }

        if (ImGui::MenuItem("End Race", nullptr, false, can_end))
        {
            std::cout << "[UI] Race Ended\n";
            g_race_manager->StopSession();
        }

        if (ImGui::MenuItem("Restart Race", nullptr, false, can_restart))
        {
            std::cout << "[UI] Race Restarted\n";
            g_race_manager->ResetSession();
        }

        ImGui::EndMenu();
    }
}
