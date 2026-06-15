#pragma once

#include <map>
#include <vector>
#include <chrono>
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
    bool isLapped;                   // True if behind leader by 1+ laps
    bool hasStartedFirstLap;         // True if vehicle started racing
    bool isFinished;                 // True if vehicle has crossed finish in Finishing state
    
    // Time difference calculations
    float deltaTimeToBest;           // Delta to best lap (from CalculateLapTimeDiff)
    float deltaTimeToLeader;         // Delta to leader (from CalculateLeaderTimeDiff)
    
    VehicleStanding() 
        : vehicleID(0), completedLaps(0), currentLapNumber(0), currentLapTime(0.0f), 
          bestLapTime(-1.0f), totalRaceTime(0.0f), distanceFromStart(0.0), 
          position(0), isLapped(false), hasStartedFirstLap(false), isFinished(false),
          deltaTimeToBest(0.0f), deltaTimeToLeader(0.0f) {}
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

    // Auto-stop mechanics
    void SetAutoStopConditions(int maxLaps, float maxSeconds);
    int GetAutoStopLaps() const;
    float GetAutoStopSeconds() const;

    // ========================================================================
    // LEADERBOARD & STANDINGS
    // ========================================================================
    std::vector<VehicleStanding> GetStandings() const;
    
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
    
private:
    // ========================================================================
    // SESSION TRACKING
    // ========================================================================
    SessionState m_sessionState = SessionState::Idle;
    std::map<int32_t, int> m_finishPositions;
    std::chrono::steady_clock::time_point m_raceStartTime;
    bool m_raceTimerRunning = false;
    float m_raceElapsedSeconds = 0.0f;

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
    
    // ========================================================================
    // INTERSECTION DETECTION (sub-frame accurate)
    // ========================================================================
    bool CheckLineSegmentIntersection(
        const glm::vec2& vehiclePrev, const glm::vec2& vehicleCurrent,
        const glm::vec2& lineP1, const glm::vec2& lineP2,
        float& outIntersectionRatio) const;
    
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
};

// ============================================================================
// GLOBAL RACE MANAGER INSTANCE
// ============================================================================
extern RaceManager* g_race_manager;
