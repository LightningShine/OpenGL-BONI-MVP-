#pragma once

#include <map>
#include <vector>
#include <chrono>
#include <mutex>
#include <glm/glm.hpp>
#include "../vehicle/Vehicle.h"
#include "StopReset/StartStop.h"

// LapData is defined in Vehicle.h

// ============================================================================
// VEHICLE STANDING (for leaderboard)
// ============================================================================
struct VehicleStanding
{
    int32_t vehicleID;
    int completedLaps;
    int currentLapNumber;            // Current lap being driven
    float currentLapTime;
    float bestLapTime;
    float totalRaceTime;             // Sum of all completed lap times
    double distanceFromStart;        // Progress along track (0.0 to 1.0)
    int position;                    // Current race position (1st, 2nd, etc.)
    int serverPosition;              // Track Server classification (0 = local session)
    bool isLapped;                   // Реально отстал на круг дистанции (протокол)
    int  lapsBehindLeader;           // Разница номеров кругов с лидером (табло)
    bool hasStartedFirstLap;         // True if vehicle started racing
    bool isFinished;                 // True if vehicle has crossed finish in Finishing state
    
    // Time difference calculations
    float deltaTimeToBest;           // Delta to best lap (from CalculateLapTimeDiff)
    float deltaTimeToLeader;         // Delta to leader (from CalculateLeaderTimeDiff)
    
    VehicleStanding() 
        : vehicleID(0), completedLaps(0), currentLapNumber(0), currentLapTime(0.0f), 
          bestLapTime(-1.0f), totalRaceTime(0.0f), distanceFromStart(0.0), 
          position(0), serverPosition(0), isLapped(false), lapsBehindLeader(0),
          hasStartedFirstLap(false),
          isFinished(false), deltaTimeToBest(0.0f), deltaTimeToLeader(0.0f) {}
};

// ============================================================================
// RACE MANAGER - Physics-based Lap Timing System
// ============================================================================
class RaceManager
{
public:
    RaceManager();
    ~RaceManager();
    
    // ========================================================================
    // CORE UPDATE LOOP (called every frame with delta time)
    // ========================================================================
    void Update(float deltaTime);
    
    // ========================================================================
    // SESSION CONTROL
    // ========================================================================
    void StartSession();
    void StopSession();
    void ResetSession();                        // Clear all lap data and timers
    void ResetMap();
    SessionState GetSessionState() const;
    float GetRaceElapsedTime() const;
    void SetStartFinishLine(const glm::vec2& p1, const glm::vec2& p2);  // Define start/finish line

    // Копия линии старт/финиша. Нужна приёму телеметрии: пересечение ищется
    // там, где рождается отрезок движения, иначе часть проездов теряется
    // между кадрами (см. Vehicle::LineCrossing).
    // false — линия ещё не задана (трек не загружен).
    bool GetStartFinishLine(glm::vec2& out_p1, glm::vec2& out_p2) const;

    // Auto-stop mechanics
    void SetAutoStopConditions(int maxLaps, float maxSeconds);
    int GetAutoStopLaps() const;
    float GetAutoStopSeconds() const;

    // ========================================================================
    // LEADERBOARD & STANDINGS
    // ========================================================================
    // Отдаёт снимок таблицы. Пересчитывается не чаще раза за кадр: за кадр её
    // спрашивают несколько независимых панелей, а результат у них общий.
    std::vector<VehicleStanding> GetStandings() const;

    // Сбрасывает снимок принудительно — после изменений, которые обязаны быть
    // видны немедленно (старт, стоп и сброс сессии).
    void InvalidateStandingsCache();
    
    // ========================================================================
    // LAP DATA ACCESS (for UI display)
    // ========================================================================
    const std::map<int, LapData>* GetVehicleLaps(int32_t vehicleID) const;
    // Thread-safe snapshot: returns a copy of the lap map taken under the
    // vehicles mutex. Use this from the UI thread instead of GetVehicleLaps(),
    // whose returned pointer outlives the lock and races the network thread.
    std::map<int, LapData> GetVehicleLapsCopy(int32_t vehicleID) const;
    float GetVehicleCurrentLapTime(int32_t vehicleID) const;
    int GetVehicleCompletedLaps(int32_t vehicleID) const;
    float GetVehicleBestLapTime(int32_t vehicleID) const;
    float GetVehiclePreviousLapTime(int32_t vehicleID) const;
    int GetVehicleCurrentLapNumber(int32_t vehicleID) const;
    
    // Time difference calculations (delegates to TimeDiff functions)
    float GetVehicleLapDelta(int32_t vehicleID) const;      // Delta to best lap
    float GetVehicleLeaderDelta(int32_t vehicleID) const;   // Delta to leader
    
    // ========================================================================
    // DIAGNOSTICS
    // ========================================================================
    void PrintSessionSummary() const;
    bool SaveResultsToFile() const;
    // Full results report as text — used by Ctrl+S (save as), Ctrl+P (print)
    // and SaveResultsToFile(). Works for local and Track Server sessions.
    std::string BuildResultsText() const;
    
private:
    // ========================================================================
    // SESSION TRACKING
    // ========================================================================
    SessionState m_sessionState = SessionState::Idle;
    std::map<int32_t, int> m_finishPositions;
    std::chrono::steady_clock::time_point m_raceStartTime;
    bool m_raceTimerRunning = false;
    float m_raceElapsedSeconds = 0.0f;

    // Протокол этой сессии уже сохранён. Финиш наступает один раз, но Update
    // после него продолжает идти — без признака протокол переписывался бы
    // каждый кадр новым файлом со свежей меткой времени.
    bool m_resultsSaved = false;

    // Auto-stop config
    int m_autoStopMaxLaps = 0;
    float m_autoStopMaxSeconds = 0.0f;

    // Finishing state tracking
    int     m_leaderLapsAtStop  = 0;   // Leader's completed laps when StopSession was called
    int32_t m_leaderAtStop      = -1;  // Leader vehicleID when StopSession was called
    int     m_leadLapCarCount   = 0;   // Cars on the lead lap at Stop (must finish before lapped cars)

    // ========================================================================
    // START/FINISH LINE (defined as two points: left and right edge)
    // ========================================================================
    glm::vec2 m_startFinishP1;
    glm::vec2 m_startFinishP2;
    bool m_lineInitialized;
    
    // Геометрия пересечения переехала в track::segment_intersection и считается
    // на приёме пакета — см. Vehicle::LineCrossing.


    // ========================================================================
    // DISTANCE CALCULATION (for leaderboard sorting)
    // ========================================================================
    double CalculateDistanceFromStart(const Vehicle& vehicle) const;
    
    // ========================================================================
    // INTERNAL STANDINGS (without mutex lock - for use within Update)
    // ========================================================================
    std::vector<VehicleStanding> GetStandingsInternal() const;
    
    // ========================================================================
    // LEADER LAP COUNT (for lapped detection)
    // ========================================================================
    int GetLeaderLapCount() const;

    // Собирает и публикует снимок для интерфейса. Зовётся в конце Update, когда
    // состояние согласовано целиком. Вызывающий держит мьютекс машин.
    void PublishSnapshot(const std::vector<VehicleStanding>& standings) const;

public:
    // Публикует снимок с СВЕЖИМИ ПОЗИЦИЯМИ и прежней таблицей.
    //
    // Нужно перемотке: пересчёт таблицы с дельтами по всем машинам стоит
    // дорого, и делать его на каждый мелкий шаг нельзя — но и показывать
    // устаревшие позиции тоже. Пока оператор мотает, обновляем то, что он
    // видит на карте, а точную гоночную логику досчитываем один раз, когда
    // перемотка остановится.
    void PublishPositionsOnly() const;

private:

    // ========================================================================
    // STANDINGS CACHE (см. GetStandings)
    // ========================================================================
    // Кадр при 60 fps длится ~16.7 мс, так что окно короче кадра: панели одного
    // кадра получают один и тот же снимок, а следующий кадр считает заново.
    static constexpr std::chrono::milliseconds STANDINGS_CACHE_TTL{ 10 };

    mutable std::mutex m_standings_cache_mutex;
    mutable std::vector<VehicleStanding> m_standings_cache;
    mutable std::chrono::steady_clock::time_point m_standings_cache_time;
};

// ============================================================================
// GLOBAL RACE MANAGER INSTANCE
// ============================================================================
extern RaceManager* g_race_manager;
