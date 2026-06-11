#include "StartStop.h"
#include "../RaceManager.h"
#include "../../rendering/Render.h"
#include "../../Config.h"
#include <iostream>
#include <chrono>

void RaceManager::StartSession() {
    ResetSession();
    m_sessionState = SessionState::Active;
    m_raceStartTime = std::chrono::steady_clock::now();
    m_raceTimerRunning = true;
    m_raceElapsedSeconds = 0.0f;
    std::cout << "[SESSION] Session Started!" << std::endl;
}

void RaceManager::StopSession() {
    if (m_sessionState == SessionState::Active) {
        m_sessionState = SessionState::Finishing;

        // Record leader state so finishing order is enforced correctly:
        // 1) The stored leader must be the first to cross the finish line.
        // 2) Lapped cars may only finish after all lead-lap cars have finished.
        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);

            int maxLaps = 0;
            for (const auto& [id, veh] : g_vehicles)
                if (veh.m_has_started_first_lap && veh.m_completed_laps > maxLaps)
                    maxLaps = veh.m_completed_laps;
            m_leaderLapsAtStop = maxLaps;

            // Break lap ties by total progress to find the actual leader
            int32_t leaderId = -1;
            double  leaderProg = -1.0;
            for (const auto& [id, veh] : g_vehicles) {
                if (veh.m_has_started_first_lap && veh.m_completed_laps == maxLaps) {
                    if (leaderId == -1 || veh.m_total_progress > leaderProg) {
                        leaderId   = id;
                        leaderProg = veh.m_total_progress;
                    }
                }
            }
            m_leaderAtStop = leaderId;

            m_leadLapCarCount = 0;
            for (const auto& [id, veh] : g_vehicles)
                if (veh.m_has_started_first_lap && veh.m_completed_laps >= maxLaps)
                    m_leadLapCarCount++;
        }

        std::cout << "[SESSION] Session Stopped! Leader=#" << m_leaderAtStop
                  << " laps=" << m_leaderLapsAtStop
                  << " leadLapCars=" << m_leadLapCarCount
                  << " Awaiting finishing laps..." << std::endl;
    }
}

void RaceManager::ResetSession() {
    m_sessionState = SessionState::Idle;
    m_finishPositions.clear();
    m_raceTimerRunning = false;
    m_raceElapsedSeconds = 0.0f;
    m_leaderLapsAtStop = 0;
    m_leaderAtStop = -1;
    m_leadLapCarCount = 0;

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
        vehicle.m_telemetry_sample_timer = 0.0f;
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

float RaceManager::GetRaceElapsedTime() const {
    if (m_raceTimerRunning) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = now - m_raceStartTime;
        return elapsed.count();
    }
    return m_raceElapsedSeconds;
}