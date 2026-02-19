#pragma once

#include <map>
#include <vector>
#include <glm/glm.hpp>
#include "../vehicle/Vehicle.h"

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
    bool hasStartedFirstLap;         // ? True if vehicle started racing
    
    VehicleStanding() 
        : vehicleID(0), completedLaps(0), currentLapNumber(0), currentLapTime(0.0f), 
          bestLapTime(-1.0f), totalRaceTime(0.0f), distanceFromStart(0.0), 
          position(0), isLapped(false), hasStartedFirstLap(false) {}  // -1 = no data yet
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
    void ResetSession();                        // Clear all lap data and timers
    void SetStartFinishLine(const glm::vec2& p1, const glm::vec2& p2);  // Define start/finish line
    
    // ========================================================================
    // LEADERBOARD & STANDINGS
    // ========================================================================
    std::vector<VehicleStanding> GetStandings() const;
    
    // ========================================================================
    // LAP DATA ACCESS (for UI display)
    // ========================================================================
    const std::map<int, LapData>* GetVehicleLaps(int32_t vehicleID) const;
    float GetVehicleCurrentLapTime(int32_t vehicleID) const;
    int GetVehicleCompletedLaps(int32_t vehicleID) const;
    float GetVehicleBestLapTime(int32_t vehicleID) const;
    
    // ? NEW: Lap Timer Data (??? ??????????? ? UI)
    float GetVehiclePreviousLapTime(int32_t vehicleID) const;       // ????? ??????????? ?????
    float GetVehicleDeltaTime(int32_t vehicleID) const;             // ??????? ? ????????? ?????? (????????????? ? Config)
    int GetVehicleCurrentLapNumber(int32_t vehicleID) const;        // ????? ???????? ?????
    
    // ========================================================================
    // DIAGNOSTICS
    // ========================================================================
    void PrintSessionSummary() const;           // Debug output of all lap times
    bool SaveResultsToFile() const;             // Save results to saves/VehicleResults.txt
    
private:
    // ========================================================================
    // INTERNAL VEHICLE TIMING DATA
    // ========================================================================
    struct VehicleTimingData
    {
        std::map<int, LapData> laps;            // Map: lap number -> LapData
        float currentLapTimer;                   // Current lap timer (seconds)
        int currentLapNumber;                    // Current lap being driven (1-indexed)
        int completedLaps;                       // Number of completed laps
        bool hasStartedFirstLap;                 // True after first S/F crossing
        double prevX, prevY;                     // Previous frame position (for intersection)
        float bestLapTime;                       // Best lap time (seconds)
        bool isInitialized;                      // ? True after first initialization
        double prevProgress;                     // ? Previous track progress (for cycle detection)
        
        VehicleTimingData() 
            : currentLapTimer(0.0f), currentLapNumber(RaceConstants::LAP_START_NUMBER), completedLaps(0),
              hasStartedFirstLap(false), prevX(0.0), prevY(0.0), 
              bestLapTime(-1.0f), isInitialized(false), prevProgress(0.0) {}  // -1 = no data yet
    };
    
    std::map<int32_t, VehicleTimingData> m_vehicleTimings;  // Per-vehicle timing data
    
    // ========================================================================
    // START/FINISH LINE (defined as two points: left and right edge)
    // ========================================================================
    glm::vec2 m_startFinishP1;                  // Left edge of finish line
    glm::vec2 m_startFinishP2;                  // Right edge of finish line
    bool m_lineInitialized;                      // True if line has been set
    
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
