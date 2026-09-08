#include "racing/StartStop.h"
#include "racing/RaceManager.h"
#include "rendering/Render.h"
#include "network/ReplayPlayer.h"
#include "network/TelemetryIngest.h"
#include "core/Config.h"
#include <iostream>
#include <chrono>

void RaceManager::StartSession() {
    ResetSession();
    m_sessionState = SessionState::Active;

    // ЗАПИСЬ ЗАЕЗДА НАЧИНАЕТСЯ ЗДЕСЬ, а не с подключения источника.
    //
    // Раньше журнал открывался вместе с приёмником, поэтому каждый свободный
    // выезд оседал в saves/replays отдельным файлом, и найти среди них
    // собственно заезд было нечем. Практика теперь считает круги наравне с
    // гонкой, но на диск не пишется — записью становится то, что оператор
    // объявил заездом.
    //
    // На повторе журнал не открываем: иначе получилась бы запись записи —
    // повтор тоже поднимает сессию, чтобы круги считались (см. replay_open).
    if (!telemetry::replay_is_active())
        telemetry::ingest_start(logging::TelemetryLogSource::Receiver);
    m_raceStartTime = std::chrono::steady_clock::now();
    m_raceTimerRunning = true;
    m_raceElapsedSeconds = 0.0f;
    // Пока сессия идёт, машины не удаляются по таймауту: потеря связи не должна
    // стирать участника вместе с его кругами (см. removeVehicles).
    g_race_session_active.store(true, std::memory_order_relaxed);
    InvalidateStandingsCache();  // смена состояния должна быть видна сразу
    std::cout << "[SESSION] Session Started!" << std::endl;
}

void RaceManager::StopSession() {
    if (m_sessionState == SessionState::Active) {
        m_sessionState = SessionState::Finishing;

        // Record leader state so finishing order is enforced correctly:
        // 1) The stored leader must be the first to cross the finish line.
        // 2) Lapped cars may only finish after all lead-lap cars have finished.
        {
            VehiclesLock lock;

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

        InvalidateStandingsCache();
        std::cout << "[SESSION] Session Stopped! Leader=#" << m_leaderAtStop
                  << " laps=" << m_leaderLapsAtStop
                  << " leadLapCars=" << m_leadLapCarCount
                  << " Awaiting finishing laps..." << std::endl;
    }
}

void RaceManager::ResetSession() {
    // Заезд кончился — закрываем его запись. Дальше идёт практика, а она на
    // диск не пишется. Деструктор писателя дописывает заголовок и хвост с
    // трассой, поэтому файл остаётся законченным, даже если сессию сбросили.
    //
    // StartSession зовёт ResetSession первой, и это не мешает: там журнал
    // открывается уже ПОСЛЕ сброса.
    telemetry::ingest_stop();

    m_sessionState = SessionState::Idle;
    m_finishPositions.clear();
    m_resultsSaved = false;   // следующий заезд сохранит свой протокол
    m_raceTimerRunning = false;
    m_raceElapsedSeconds = 0.0f;
    m_leaderLapsAtStop = 0;
    m_leaderAtStop = -1;
    m_leadLapCarCount = 0;
    // Сессии нет — машины снова живут по таймауту и уходят с карты сами.
    g_race_session_active.store(false, std::memory_order_relaxed);

    VehiclesLock lock;
    for (auto& [id, vehicle] : g_vehicles) {
        vehicle.m_laps.clear();
        vehicle.laps.clear(); // Clear telemetry samples
        vehicle.m_current_lap_timer = 0.0f;
        vehicle.m_current_lap_number = RaceConstants::OUT_LAP_NUMBER;
        vehicle.m_completed_laps = 0;
        vehicle.m_total_progress = 0.0;
        vehicle.m_has_started_first_lap = false;
        vehicle.m_best_lap_time = -1.0f;
        vehicle.bestlapID = -1;
        vehicle.m_prev_track_progress = 0.0;
        vehicle.m_is_finished = false;
        vehicle.m_telemetry_sample_timer = 0.0f;
        vehicle.m_lap_armed = false;
        vehicle.m_is_lapped = false;
        vehicle.m_laps_behind_leader = 0;
        vehicle.m_distance_laps_behind = 0;
        vehicle.m_lap_start_utc_ms = 0;
        vehicle.m_pending_crossings.clear();
        vehicle.m_current_lap_sectors.fill(SECTOR_TIME_NONE);
        vehicle.m_current_sector = 0;
        vehicle.m_sector_start_utc_ms = 0;
    }
    InvalidateStandingsCache();

    // Публикуем состояние ПОСЛЕ сброса. Панели читают только снимок, поэтому
    // без публикации они держали бы прежнюю таблицу и круги до следующего
    // Update — а если машин уже нет, то и до бесконечности.
    PublishSnapshot({});

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