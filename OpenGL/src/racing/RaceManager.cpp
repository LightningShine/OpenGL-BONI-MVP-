#include "RaceManager.h"
#include "../vehicle/Vehicle.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"
#include "TimeDiffirence/TimeDiff.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <sstream>

// ============================================================================
// EXTERNAL GLOBALS
// ============================================================================
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;
extern std::vector<SplinePoint> g_smooth_track_points;
extern std::atomic<bool> g_is_map_loaded;

// ============================================================================
// GLOBAL RACE MANAGER INSTANCE
// ============================================================================
RaceManager* g_race_manager = nullptr;

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================
RaceManager::RaceManager()
    : m_lineInitialized(false)
    , m_startFinishP1(0.0f, 0.0f)
    , m_startFinishP2(0.0f, 0.0f)
{
    std::cout << "[RACE MANAGER] Initialized" << std::endl;
}

RaceManager::~RaceManager()
{
    std::cout << "[RACE MANAGER] Destroyed" << std::endl;
}

// ============================================================================
// SET START/FINISH LINE
// ============================================================================
void RaceManager::SetStartFinishLine(const glm::vec2& p1, const glm::vec2& p2)
{
    m_startFinishP1 = p1;
    m_startFinishP2 = p2;
    m_lineInitialized = true;

    std::cout << "[RACE MANAGER] Start/Finish line set: "
              << "P1(" << p1.x << ", " << p1.y << ") -> "
              << "P2(" << p2.x << ", " << p2.y << ")" << std::endl;
}

// ============================================================================
// AUTO STOP
// ============================================================================
void RaceManager::SetAutoStopConditions(int maxLaps, float maxSeconds)
{
    m_autoStopMaxLaps = maxLaps;
    m_autoStopMaxSeconds = maxSeconds;
}

int RaceManager::GetAutoStopLaps() const
{
    return m_autoStopMaxLaps;
}

float RaceManager::GetAutoStopSeconds() const
{
    return m_autoStopMaxSeconds;
}

// ============================================================================
// CORE UPDATE LOOP - Reads/writes Vehicle data directly under mutex
// ============================================================================
void RaceManager::Update(float deltaTime)
{
    if (!g_is_map_loaded || !m_lineInitialized)
        return;

    if (m_sessionState == SessionState::Ended)
        return;

    if (m_sessionState == SessionState::Active)
    {
        bool timeHit = (m_autoStopMaxSeconds > 0.0f && GetRaceElapsedTime() >= m_autoStopMaxSeconds);
        bool lapHit = false;

        if (m_autoStopMaxLaps > 0)
        {
            for (const auto& [id, veh] : g_vehicles)
            {
                if (veh.m_completed_laps >= m_autoStopMaxLaps)
                {
                    lapHit = true;
                    break;
                }
            }
        }

        if (timeHit || lapHit)
        {
            StopSession();
        }
    }

    std::lock_guard<std::mutex> lock(g_vehicles_mutex);

    for (auto& [vehicleID, vehicle] : g_vehicles)
    {
        constexpr bool kDebugFinishCrossing = true;
        // ====================================================================
        // TELEMETRY RECORDING (every frame during active lap)
        // Records vehicle state for TimeDiff calculations.
        // IMPORTANT: On clients, vehicles can be authoritative (replicated)
        // and we still need to record samples, otherwise TimeDiff can't work.
        // ====================================================================
        if (vehicle.m_has_started_first_lap && !vehicle.m_is_finished)
        {
            constexpr float kTelemetrySampleInterval = 0.1f; // 10 Hz
            constexpr size_t kMaxSamplesPerLap = 36000;       // 1 hour cap per lap
            vehicle.m_telemetry_sample_timer += deltaTime;

            if (vehicle.m_telemetry_sample_timer >= kTelemetrySampleInterval)
            {
                vehicle.m_telemetry_sample_timer = 0.0f;

                LapInfo sample;
                sample.timefromstart = vehicle.m_current_lap_timer;
                sample.progress = vehicle.m_track_progress;
                sample.timestamp = std::chrono::steady_clock::now();
                sample.total_progress = vehicle.m_total_progress;
                sample.gForceX = static_cast<float>(vehicle.m_g_force_x);
                sample.gForceY = static_cast<float>(vehicle.m_g_force_y);
                sample.aceleration = static_cast<float>(vehicle.m_acceleration);
                sample.speed = static_cast<float>(vehicle.m_speed_kph);
                sample.curentPosition = 0; // Updated after standings sort

                if (vehicle.laps.find(vehicle.m_current_lap_number) == vehicle.laps.end())
                {
                    vehicle.laps[vehicle.m_current_lap_number] = CarLapSessions();
                    vehicle.laps[vehicle.m_current_lap_number].lapnumber = vehicle.m_current_lap_number;
                }
                auto& currentLapSamples = vehicle.laps[vehicle.m_current_lap_number].samples;
                if (currentLapSamples.size() < kMaxSamplesPerLap)
                    currentLapSamples.push_back(sample);
            }
        }

        // Vehicles driven by processed server state already have authoritative
        // lap/progress/timing values. Do not advance them locally on the client.
        if (vehicle.m_has_authoritative_state)
        {
            // Client-side: do not advance timing/lap counters locally.
            // Still keep derived fields consistent for standings/time-diff.
            // If server provides a valid best lap time, keep it visible in UI.
            if (vehicle.m_best_lap_time >= 0.0f)
            {
                // Best lap ID is used by TimeDiff; if it is unset, point it to the
                // latest completed lap (bestlap time itself is authoritative).
                if (vehicle.bestlapID < RaceConstants::LAP_START_NUMBER)
                {
                    const int candidate = vehicle.m_current_lap_number - 1;
                    if (candidate >= RaceConstants::LAP_START_NUMBER)
                        vehicle.bestlapID = candidate;
                }
            }

            vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
            continue;
        }
        
        // Check for start/finish crossing.
        // Use strict segment intersection for correctness. Progress-cycle fallback below
        // handles low-rate updates on closed tracks.
        // The S/F line lives in the centered track frame; GPS vehicles store raw
        // coordinates, so shift them by the same render offset the drawing uses
        // (for .trk2 tracks the offset is non-zero — without it no lap counts).
        const glm::vec2 frameOff = vehicle.m_apply_track_render_offset
                                     ? getTrackRenderOffset() : glm::vec2(0.0f, 0.0f);
        const glm::vec2 prevPos(vehicle.m_prev_x + frameOff.x, vehicle.m_prev_y + frameOff.y);
        const glm::vec2 curPos(vehicle.m_normalized_x + frameOff.x, vehicle.m_normalized_y + frameOff.y);

        const glm::vec2 sfP1 = m_startFinishP1;
        const glm::vec2 sfP2 = m_startFinishP2;

        float intersectionRatio = 0.0f;
        const bool crossed = CheckLineSegmentIntersection(prevPos, curPos, sfP1, sfP2, intersectionRatio);
        
        // Check for progress cycle (0.999 -> 0.001)
        // Use this only for real GNSS telemetry (jitter/low-rate updates). Simulation and
        // other non-GNSS sources should rely on strict line intersection to avoid false laps.
        const bool isNonGnssSource = (vehicle.m_fix_type < 2);
        bool progressCycled = false;
        if (!isNonGnssSource)
        {
            progressCycled = (vehicle.m_prev_track_progress > 0.85 &&
                              vehicle.m_track_progress < 0.15);
        }
        
        // ====================================================================
        // LAP COMPLETION DETECTION
        // ====================================================================
        if (kDebugFinishCrossing && (crossed || progressCycled))
        {
            const glm::vec2 off = getTrackRenderOffset();
            std::cout.setf(std::ios::fixed);
            std::cout << "[S/F DEBUG] veh#" << vehicleID
                      << " sessionState=" << static_cast<int>(m_sessionState)
                      << " finished=" << (vehicle.m_is_finished ? 1 : 0)
                      << " crossed=" << (crossed ? 1 : 0)
                      << " ratio=" << std::setprecision(3) << intersectionRatio
                      << std::endl;
        }

        if (m_sessionState == SessionState::Idle)
        {
            // Just riding, reset timer if crossed but don't record laps.
            if (crossed || progressCycled) {
                vehicle.m_current_lap_timer = deltaTime * (1.0f - intersectionRatio);
            } else {
                vehicle.m_current_lap_timer += deltaTime;
            }
            vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
            continue;
        }

        if (vehicle.m_is_finished)
        {
            // Just driving after finishing. Ignore laps.
            vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
            continue;
        }

        if (vehicle.m_has_started_first_lap && (crossed || progressCycled))
        {
            // Sub-frame accurate timing
            float crossingTime = vehicle.m_current_lap_timer + (deltaTime * intersectionRatio);

            const float MIN_VALID_LAP_TIME = 0.1f;

            if (crossingTime > MIN_VALID_LAP_TIME)
            {
                // ----------------------------------------------------------------
                // In Finishing state, enforce correct finishing order:
                //   1) The stored leader must finish first.
                //   2) Same-lap cars may finish only after the leader has crossed.
                //   3) Lapped cars may finish only after ALL same-lap cars are done.
                // If a car is not yet allowed to finish, skip recording this
                // crossing entirely — the timer resets below and it tries again.
                // ----------------------------------------------------------------
                bool processLap = true;
                if (m_sessionState == SessionState::Finishing)
                {
                    const bool isTheLeader       = (vehicleID == m_leaderAtStop);
                    const bool leaderHasFinished = (m_leaderAtStop >= 0 &&
                                                    m_finishPositions.count(m_leaderAtStop) > 0);
                    // completed_laps not yet incremented here — compare against stored baseline
                    const bool isLeadLapCar      = (vehicle.m_completed_laps >= m_leaderLapsAtStop);
                    const bool allLeadLapFinished = (static_cast<int>(m_finishPositions.size()) >= m_leadLapCarCount);

                    processLap = (isTheLeader ||
                                  (isLeadLapCar  && leaderHasFinished) ||
                                  (!isLeadLapCar && allLeadLapFinished));
                }

                if (processLap)
                {
                    // Store completed lap
                    LapData lapData(crossingTime, 0);
                    vehicle.m_laps[vehicle.m_current_lap_number] = lapData;
                    vehicle.m_completed_laps++;

                    // Update best lap time and ID
                    if (crossingTime < vehicle.m_best_lap_time || vehicle.m_best_lap_time < 0.0f)
                    {
                        vehicle.m_best_lap_time = crossingTime;
                        vehicle.bestlapID = vehicle.m_current_lap_number;
                    }

                    std::cout << "[RACE MANAGER] Vehicle #" << vehicleID
                              << " completed Lap " << vehicle.m_current_lap_number
                              << " in " << std::fixed << std::setprecision(3) << crossingTime << "s"
                              << " | Total completed: " << vehicle.m_completed_laps << std::endl;

                    if (m_sessionState == SessionState::Finishing)
                    {
                        vehicle.m_is_finished = true;
                        if (m_finishPositions.find(vehicleID) == m_finishPositions.end())
                            m_finishPositions[vehicleID] = static_cast<int>(m_finishPositions.size() + 1);
                        vehicle.m_current_lap_timer = 0.0f;
                        std::cout << "[RACE MANAGER] Vehicle #" << vehicleID
                                  << " HAS FINISHED! pos=" << m_finishPositions[vehicleID] << std::endl;
                    }
                    else
                    {
                        vehicle.m_current_lap_number++;
                    }
                }
                // else: car not yet allowed to finish — timer resets below, it retries next crossing
            }

            // Reset timer after crossing (applies even when processLap==false)
            vehicle.m_current_lap_timer = deltaTime * (1.0f - intersectionRatio);
        }
        // ====================================================================
        // FIRST LAP START DETECTION
        // ====================================================================
        else if (!vehicle.m_has_started_first_lap && crossed)
        {
            // Prevent false start on vehicle creation
            float timeSinceCreation = vehicle.m_current_lap_timer;
            
            if (timeSinceCreation > 0.5f)
            {
                if (kDebugFinishCrossing)
                {
                    const glm::vec2 off = getTrackRenderOffset();
                    std::cout.setf(std::ios::fixed);
                    std::cout << "[S/F DEBUG] START veh#" << vehicleID
                              << " fixType=" << vehicle.m_fix_type
                              << " ratio=" << std::setprecision(3) << intersectionRatio
                              << " prevPos=(" << std::setprecision(6) << prevPos.x << "," << prevPos.y << ")"
                              << " curPos=(" << curPos.x << "," << curPos.y << ")"
                              << " sfP1=(" << sfP1.x << "," << sfP1.y << ")"
                              << " sfP2=(" << sfP2.x << "," << sfP2.y << ")"
                              << " off=(" << off.x << "," << off.y << ")"
                              << " prevProg=" << std::setprecision(3) << vehicle.m_prev_track_progress
                              << " curProg=" << vehicle.m_track_progress
                              << " lapT=" << vehicle.m_current_lap_timer
                              << std::endl;
                }

                vehicle.m_has_started_first_lap = true;
                vehicle.m_current_lap_timer = deltaTime * (1.0f - intersectionRatio);
                vehicle.m_prev_track_progress = vehicle.m_track_progress;
                
                std::cout << "[RACE MANAGER] Vehicle #" << vehicleID 
                          << " crossed start/finish line, starting Lap " << vehicle.m_current_lap_number 
                          << std::endl;
            }
            else
            {
                vehicle.m_current_lap_timer += deltaTime;
            }
        }
        // ====================================================================
        // NORMAL TIMER INCREMENT
        // ====================================================================
        else
        {
            vehicle.m_current_lap_timer += deltaTime;
        }

        // ====================================================================
        // UPDATE TOTAL PROGRESS (lap number + current lap progress)
        // This is essential for CalculateLeaderTimeDiffInternal to work
        // Total progress allows comparing vehicles on different laps
        // Example: Lap 2, progress 0.35 -> total_progress = 2.35
        // ====================================================================
        vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
    }

    // Check if everyone finished
    if (m_sessionState == SessionState::Finishing) {
        bool allFinished = true;
        for (const auto& [id, veh] : g_vehicles) {
            if (veh.m_has_started_first_lap && !veh.m_is_finished) {
                allFinished = false;
                break;
            }
        }
        if (allFinished && !g_vehicles.empty()) {
            m_sessionState = SessionState::Ended;
            if (m_raceTimerRunning)
            {
                auto now = std::chrono::steady_clock::now();
                std::chrono::duration<float> elapsed = now - m_raceStartTime;
                m_raceElapsedSeconds = elapsed.count();
                m_raceTimerRunning = false;
            }
            std::cout << "[SESSION] Session Ended! All cars have finished." << std::endl;
        }
    }

    // Update leader and positions
    std::vector<VehicleStanding> standings = GetStandingsInternal();
    
    // ====================================================================
    // UPDATE CURRENT POSITION IN TELEMETRY SAMPLES
    // ====================================================================
    for (size_t i = 0; i < standings.size(); ++i)
    {
        auto veh_it = g_vehicles.find(standings[i].vehicleID);
        if (veh_it != g_vehicles.end())
        {
            auto& vehicle = veh_it->second;
            if (vehicle.m_has_started_first_lap && !vehicle.laps.empty())
            {
                auto lap_it = vehicle.laps.find(vehicle.m_current_lap_number);
                if (lap_it != vehicle.laps.end() && !lap_it->second.samples.empty())
                {
                    lap_it->second.samples.back().curentPosition = static_cast<int>(i + 1);
                }
            }
        }
    }
    
    if (!standings.empty())
    {
        constexpr bool kLogLeaderChanges = false;
        static int32_t previousLeader = -1;
        int32_t currentLeader = standings[0].vehicleID;
        
        // Reset all leader flags
        for (auto& [id, vehicle] : g_vehicles)
        {
            vehicle.m_is_leader = false;
        }
        
        // Set current leader flag
        auto leader_it = g_vehicles.find(currentLeader);
        if (leader_it != g_vehicles.end())
        {
            leader_it->second.m_is_leader = true;
        }
        
        // Debug: print leader change
        if (kLogLeaderChanges && currentLeader != previousLeader && previousLeader != -1)
        {
            std::cout << "\n[LEADER CHANGE] New leader: Vehicle #" << currentLeader 
                      << " | Laps: " << standings[0].completedLaps 
                      << " | Progress: " << std::fixed << std::setprecision(3) << standings[0].distanceFromStart
                      << " (was Vehicle #" << previousLeader << ")" << std::endl;
            
            size_t topCount = standings.size() < 3 ? standings.size() : 3;
            for (size_t i = 0; i < topCount; ++i)
            {
                std::cout << "  " << (i+1) << ". Vehicle #" << standings[i].vehicleID 
                          << " | Laps: " << standings[i].completedLaps
                          << " | Progress: " << std::fixed << std::setprecision(3) << standings[i].distanceFromStart
                          << std::endl;
            }
            std::cout << std::endl;
        }
        
        previousLeader = currentLeader;
    }
}

// ============================================================================
// LINE SEGMENT INTERSECTION (2D)
// Returns true if segments intersect, and outIntersectionRatio (0.0 to 1.0)
// indicates where along the vehicle's path the intersection occurred.
// ============================================================================
bool RaceManager::CheckLineSegmentIntersection(
    const glm::vec2& vehiclePrev, const glm::vec2& vehicleCurrent,
    const glm::vec2& lineP1, const glm::vec2& lineP2,
    float& outIntersectionRatio) const
{
    // Vehicle movement vector
    glm::vec2 v = vehicleCurrent - vehiclePrev;
    
    // Start/Finish line vector
    glm::vec2 s = lineP2 - lineP1;
    
    // Check if parallel (cross product near zero)
    float denominator = (-s.x * v.y) + (v.x * s.y);
    if (std::abs(denominator) < 1e-6f)
        return false;  // Parallel or coincident
    
    // Calculate parametric intersection points
    glm::vec2 delta = vehiclePrev - lineP1;
    float t = ((-s.y * delta.x) + (s.x * delta.y)) / denominator;  // t along vehicle path
    float u = ((-v.y * delta.x) + (v.x * delta.y)) / denominator;  // u along finish line
    
    // Check if intersection is within both segments
    if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f)
    {
        outIntersectionRatio = t;
        return true;
    }
    
    return false;
}

    auto formatTime = [](float totalSeconds) {
        if (totalSeconds < 0.0f)
            totalSeconds = 0.0f;

        int minutes = static_cast<int>(totalSeconds) / 60;
        float seconds = std::fmod(totalSeconds, 60.0f);

        std::ostringstream ss;
        ss << minutes << ":" << std::setw(6) << std::setfill('0')
           << std::fixed << std::setprecision(3) << seconds;
        return ss.str();
    };

// ============================================================================
// GET STANDINGS (sorted leaderboard) - Internal version without mutex lock
// ============================================================================
std::vector<VehicleStanding> RaceManager::GetStandingsInternal() const
{
    std::vector<VehicleStanding> standings;
    const bool useFinishOrder = (m_sessionState == SessionState::Finishing || m_sessionState == SessionState::Ended);
    
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        VehicleStanding standing;
        standing.vehicleID = vehicleID;
        standing.completedLaps = vehicle.m_completed_laps;
        standing.currentLapNumber = vehicle.m_current_lap_number;
        standing.currentLapTime = vehicle.m_is_finished ? 0.0f : vehicle.m_current_lap_timer;
        standing.hasStartedFirstLap = vehicle.m_has_started_first_lap;
        standing.isFinished = vehicle.m_is_finished;
        standing.distanceFromStart = vehicle.m_track_progress;
        standing.serverPosition = vehicle.m_has_authoritative_state
                                    ? vehicle.m_server_position : 0;
        
        // Best lap logic depends on LAP_START_NUMBER
        int minCompletedLaps = (RaceConstants::LAP_START_NUMBER == 0) ? 1 : 2;
        standing.bestLapTime = (vehicle.m_completed_laps >= minCompletedLaps) ? vehicle.m_best_lap_time : -1.0f;
        
        // Calculate total race time (sum of all completed laps)
        standing.totalRaceTime = 0.0f;
        for (const auto& [lapNum, lapData] : vehicle.m_laps)
        {
            standing.totalRaceTime += lapData.lapTime;
        }
        
        // ====================================================================
        // CALCULATE TIME DIFFERENCES (using Internal versions - no mutex)
        // ====================================================================
        if (vehicle.m_is_finished || m_sessionState == SessionState::Ended)
        {
            standing.deltaTimeToBest = 0.0f;
            standing.deltaTimeToLeader = 0.0f;
        }
        else
        {
            standing.deltaTimeToBest = CalculateLapTimeDiffInternal(vehicleID);
            standing.deltaTimeToLeader = CalculateLeaderTimeDiffInternal(vehicleID);
        }
        
        standings.push_back(standing);
    }
    
    // ========================================================================
    // SORT: 0) Track Server position when authoritative, else
    //       1) Started racing? 2) Completed laps (desc), 3) Progress (desc)
    // ========================================================================
    std::sort(standings.begin(), standings.end(),
        [this, useFinishOrder](const VehicleStanding& a, const VehicleStanding& b) -> bool
        {
            // Networked session: the server's classification is the truth. It
            // freezes at the checkered flag, so a finished leader can never be
            // visually overtaken by live track progress after the line.
            if (a.serverPosition > 0 && b.serverPosition > 0)
                return a.serverPosition < b.serverPosition;
            if ((a.serverPosition > 0) != (b.serverPosition > 0))
                return a.serverPosition > 0;

            if (a.hasStartedFirstLap != b.hasStartedFirstLap)
                return a.hasStartedFirstLap > b.hasStartedFirstLap;

            if (useFinishOrder)
            {
                const auto aIt = m_finishPositions.find(a.vehicleID);
                const auto bIt = m_finishPositions.find(b.vehicleID);
                const bool aFinished = (aIt != m_finishPositions.end());
                const bool bFinished = (bIt != m_finishPositions.end());

                if (aFinished != bFinished)
                    return aFinished > bFinished;

                if (aFinished && bFinished)
                    return aIt->second < bIt->second;
            }

            if (a.completedLaps != b.completedLaps)
                return a.completedLaps > b.completedLaps;

            return a.distanceFromStart > b.distanceFromStart;
        }
    );
    
    // ========================================================================
    // ASSIGN POSITIONS & DETECT LAPPED CARS
    // ========================================================================
    int leaderLaps = standings.empty() ? 0 : standings[0].completedLaps;
    
    for (size_t i = 0; i < standings.size(); ++i)
    {
        standings[i].position = static_cast<int>(i + 1);
        standings[i].isLapped = (standings[i].completedLaps < leaderLaps);
    }
    
    return standings;
}

// ============================================================================
// GET STANDINGS (sorted leaderboard) - ????????? ?????? ? ???????????
// ?????????? ????? (?? UI, SaveResults ? ?.?.)
// ============================================================================
std::vector<VehicleStanding> RaceManager::GetStandings() const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    return GetStandingsInternal();
}

// ============================================================================
// CALCULATE DISTANCE FROM START (0.0 to 1.0)
// ? ?????: ?????????? ?????? ???????? ?? ????????? ?????? ?????? ????????? ?????
// ??? ?????? ???????? "???????" ????????? ?? S-???????? ???????? ?????
// ============================================================================
double RaceManager::CalculateDistanceFromStart(const Vehicle& vehicle) const
{
    // ? ?????????? ??????????? ???????? ?? ????????? (0.0-1.0)
    // ??? ?????? ????????? ????? ?????, ?? ??????? ?? ?????????????? ???????? ?????
    return vehicle.m_track_progress;
}

// ============================================================================
// GET LEADER LAP COUNT
// ============================================================================
int RaceManager::GetLeaderLapCount() const
{
    int maxLaps = 0;
    
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        if (vehicle.m_completed_laps > maxLaps)
            maxLaps = vehicle.m_completed_laps;
    }
    
    return maxLaps;
}

// ============================================================================
// LAP DATA ACCESS (thread-safe, reads from Vehicle under mutex)
// ============================================================================
const std::map<int, LapData>* RaceManager::GetVehicleLaps(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
        return &(it->second.m_laps);
    
    return nullptr;
}

std::map<int, LapData> RaceManager::GetVehicleLapsCopy(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
        return it->second.m_laps;   // copy under lock

    return {};
}

float RaceManager::GetVehicleCurrentLapTime(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
        return it->second.m_is_finished ? 0.0f : it->second.m_current_lap_timer;
    
    return 0.0f;
}

int RaceManager::GetVehicleCompletedLaps(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
        return it->second.m_completed_laps;
    
    return 0;
}

float RaceManager::GetVehicleBestLapTime(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
    {
        int minCompletedLaps = (RaceConstants::LAP_START_NUMBER == 0) ? 1 : 2;
        
        if (it->second.m_completed_laps < minCompletedLaps)
            return -1.0f;
        
        return it->second.m_best_lap_time;
    }
    
    return -1.0f;
}

float RaceManager::GetVehiclePreviousLapTime(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
    {
        const auto& vehicle = it->second;
        int previousLapNumber = vehicle.m_current_lap_number - 1;
        
        if (previousLapNumber < RaceConstants::LAP_START_NUMBER)
            return -1.0f;
        
        auto lapIt = vehicle.m_laps.find(previousLapNumber);
        if (lapIt != vehicle.m_laps.end())
            return lapIt->second.lapTime;
    }
    
    return -1.0f;
}

// ============================================================================
// TIME DIFFERENCE CALCULATIONS (delegates to TimeDiff functions)
// ============================================================================
float RaceManager::GetVehicleLapDelta(int32_t vehicleID) const
{
    if (m_sessionState == SessionState::Ended)
        return 0.0f;

    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
        auto it = g_vehicles.find(vehicleID);
        if (it != g_vehicles.end() && it->second.m_is_finished)
            return 0.0f;
    }

    return CalculateLapTimeDiff(vehicleID); // Thread-safe (has mutex inside)
}

float RaceManager::GetVehicleLeaderDelta(int32_t vehicleID) const
{
    if (m_sessionState == SessionState::Ended)
        return 0.0f;

    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
        auto it = g_vehicles.find(vehicleID);
        if (it != g_vehicles.end() && it->second.m_is_finished)
            return 0.0f;
    }

    return CalculateLeaderTimeDiff(vehicleID); // Thread-safe (has mutex inside)
}

int RaceManager::GetVehicleCurrentLapNumber(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
        return it->second.m_current_lap_number;
    
    return 0;
}

// ============================================================================
// DEBUG: PRINT SESSION SUMMARY
// ============================================================================
void RaceManager::PrintSessionSummary() const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "       RACE SESSION SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        std::cout << "\nVehicle #" << vehicleID << ":" << std::endl;
        std::cout << "  Completed Laps: " << vehicle.m_completed_laps << std::endl;
        std::cout << "  Current Lap Time: " << vehicle.m_current_lap_timer << "s" << std::endl;
        std::cout << "  Best Lap: " << vehicle.m_best_lap_time << "s" << std::endl;
        
        std::cout << "  Lap Times:" << std::endl;
        for (auto it = vehicle.m_laps.begin(); it != vehicle.m_laps.end(); ++it)
        {
            std::cout << "    Lap " << it->first << ": " << it->second.lapTime << "s" << std::endl;
        }
    }
    
    std::cout << "\n========================================\n" << std::endl;
}

// ============================================================================
// RESULTS REPORT — one text generator for Ctrl+S (save as .txt), Ctrl+P
// (print) and the timestamped auto-save. Works for local (prototype/COM)
// sessions and Track Server sessions alike: standings already come out in
// server classification order when connected.
// ============================================================================
std::string RaceManager::BuildResultsText() const
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    std::ostringstream file;

    file << "========================================\n";
    file << "       RACE SESSION RESULTS\n";
    file << "========================================\n";
    file << "Date: " << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << "\n";
    file << "========================================\n\n";
    
    // ???????? standings ??? ??????????
    std::vector<VehicleStanding> standings = GetStandings();
    
    if (standings.empty())
    {
        file << "No vehicles participated in this session.\n";
    }
    else
    {
        file << "FINAL STANDINGS:\n";
        file << "----------------\n\n";
        
        for (const auto& standing : standings)
        {
            file << "Position: " << standing.position;
            if (standing.isLapped) file << " (LAPPED)";
            file << "\n";
            file << "  Vehicle ID: #" << standing.vehicleID << "\n";
            file << "  Completed Laps: " << standing.completedLaps << "\n";
            
            // Progress (distance from start: 0.000 to 0.999)
            file << "  Progress: " << std::fixed << std::setprecision(3) 
                 << standing.distanceFromStart << "\n";
            
            // ====================================================================
            // TIME DIFFERENCES
            // ====================================================================
            if (standing.deltaTimeToBest != 0.0f && standing.bestLapTime > 0.0f)
            {
                file << "  Delta to Best: ";
                if (standing.deltaTimeToBest > 0)
                    file << "+" << std::fixed << std::setprecision(3) << standing.deltaTimeToBest << "s\n";
                else
                    file << std::fixed << std::setprecision(3) << standing.deltaTimeToBest << "s\n";
            }
            
            if (!standing.isLapped && standing.deltaTimeToLeader != 0.0f)
            {
                file << "  Delta to Leader: ";
                if (standing.deltaTimeToLeader > 0)
                    file << "+" << std::fixed << std::setprecision(3) << standing.deltaTimeToLeader << "s\n";
                else
                    file << std::fixed << std::setprecision(3) << standing.deltaTimeToLeader << "s\n";
            }
            
            // Total race time (sum of all laps + current lap)
            if (standing.completedLaps > 0 || standing.currentLapTime > 0.0f)
            {
                file << "  Total Race Time: " << formatTime(standing.totalRaceTime) << "\n";
            }
            
            if (standing.bestLapTime < 999999.0f)
            {
                file << "  Best Lap Time: " << formatTime(standing.bestLapTime) << "\n";
            }
            else
            {
                file << "  Best Lap Time: N/A\n";
            }
            
            // Get vehicle data for detailed lap info
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            auto veh_it = g_vehicles.find(standing.vehicleID);
            if (veh_it != g_vehicles.end())
            {
                const Vehicle& vehicle = veh_it->second;
                
                // Current lap (in progress)
                if (vehicle.m_current_lap_timer > 0.0f)
                {
                    int current_lap_num = vehicle.m_current_lap_number;
                    file << "  Current Lap " << current_lap_num << ": "
                         << formatTime(vehicle.m_current_lap_timer) << " (in progress)\n";
                }
                
                // Completed laps
                if (!vehicle.m_laps.empty())
                {
                    file << "  Lap Times:\n";
                    for (auto lap_it = vehicle.m_laps.begin(); lap_it != vehicle.m_laps.end(); ++lap_it)
                    {
                        file << "    Lap " << lap_it->first << ": "
                             << formatTime(lap_it->second.lapTime) << "\n";
                    }
                }
            }
            
            file << "\n";
        }
    }
    
    file << "========================================\n";
    file << "End of Report\n";
    file << "========================================\n";

    return file.str();
}

// ============================================================================
// SAVE RESULTS TO FILE (timestamped, saves/ directory)
// ============================================================================
bool RaceManager::SaveResultsToFile() const
{
    std::filesystem::create_directories("saves");

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    char filename[256];
    std::snprintf(filename, sizeof(filename),
                 "saves/VehicleResults_%04d-%02d-%02d_%02d-%02d-%02d.txt",
                 tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                 tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "[RACE MANAGER] Failed to create file: " << filename << std::endl;
        return false;
    }
    file << BuildResultsText();
    file.close();

    std::cout << "[RACE MANAGER] Results saved to: " << filename << std::endl;
    return true;
}
