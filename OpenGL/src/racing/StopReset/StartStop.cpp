#include "StartStop.h"
#include "../RaceManager.h"
#include "../../rendering/Render.h"
#include "../../Config.h"
#include <iostream>

void RaceManager::StartSession() {
    ResetSession();
    m_sessionState = SessionState::Active;
    std::cout << "[SESSION] Session Started!" << std::endl;
}

void RaceManager::StopSession() {
    if (m_sessionState == SessionState::Active) {
        m_sessionState = SessionState::Finishing;
        std::cout << "[SESSION] Session Stopped! Awaiting finishing laps..." << std::endl;
    }
}

void RaceManager::ResetSession() {
    m_sessionState = SessionState::Idle;

    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    for (auto& [id, vehicle] : g_vehicles) {
        vehicle.m_laps.clear();
        vehicle.laps.clear(); // Clear telemetry samples
        vehicle.m_current_lap_timer = 0.0f;
        vehicle.m_current_lap_number = RaceConstants::LAP_START_NUMBER;
        vehicle.m_completed_laps = 0;
        vehicle.m_total_progress = 0.0;
        vehicle.m_has_started_first_lap = false;
        vehicle.m_best_lap_time = -1.0f;
        vehicle.bestlapID = -1;
        vehicle.m_prev_track_progress = 0.0;
        vehicle.m_is_finished = false;
    }
    std::cout << "[SESSION] Session Reset! All lap data cleared." << std::endl;
}

void RaceManager::ResetMap() {
    ResetSession();
    TrackRenderer::clearTrackCache();
    std::cout << "[SESSION] Track & Map Reset!" << std::endl;
}

SessionState RaceManager::GetSessionState() const {
    return m_sessionState;
}