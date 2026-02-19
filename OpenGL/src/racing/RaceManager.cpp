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
// CORE UPDATE LOOP - Reads/writes Vehicle data directly under mutex
// ============================================================================
void RaceManager::Update(float deltaTime)
{
    if (!g_is_map_loaded || !m_lineInitialized)
        return;
    
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    
    for (auto& [vehicleID, vehicle] : g_vehicles)
    {
        // Initialize prev position on first frame
        if (vehicle.m_prev_x == 0.0 && vehicle.m_prev_y == 0.0 && 
            vehicle.m_normalized_x != 0.0 && vehicle.m_normalized_y != 0.0)
        {
            vehicle.m_prev_x = vehicle.m_normalized_x;
            vehicle.m_prev_y = vehicle.m_normalized_y;
            vehicle.m_prev_track_progress = vehicle.m_track_progress;
        }
        
        // ====================================================================
        // TELEMETRY RECORDING (every frame during active lap)
        // Records vehicle state for TimeDiff calculations
        // ====================================================================
        if (vehicle.m_has_started_first_lap)
        {
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
            
            // Create lap entry if it doesn't exist
            if (vehicle.laps.find(vehicle.m_current_lap_number) == vehicle.laps.end())
            {
                vehicle.laps[vehicle.m_current_lap_number] = CarLapSessions();
                vehicle.laps[vehicle.m_current_lap_number].lapnumber = vehicle.m_current_lap_number;
            }
            vehicle.laps[vehicle.m_current_lap_number].samples.push_back(sample);
        }
        
        // Check for finish line crossing
        float intersectionRatio = 0.0f;
        bool crossed = CheckLineSegmentIntersection(
            glm::vec2(vehicle.m_prev_x, vehicle.m_prev_y),
            glm::vec2(vehicle.m_normalized_x, vehicle.m_normalized_y),
            m_startFinishP1, m_startFinishP2,
            intersectionRatio
        );
        
        // Check for progress cycle (0.999 -> 0.001)
        bool progressCycled = (vehicle.m_prev_track_progress > 0.85 && 
                               vehicle.m_track_progress < 0.15);
        
        // ====================================================================
        // LAP COMPLETION DETECTION
        // ====================================================================
        if (vehicle.m_has_started_first_lap && (crossed || progressCycled))
        {
            // Sub-frame accurate timing
            float crossingTime = vehicle.m_current_lap_timer + (deltaTime * intersectionRatio);
            
            const float MIN_VALID_LAP_TIME = 0.1f;
            
            if (crossingTime > MIN_VALID_LAP_TIME)
            {
                // Store completed lap
                LapData lapData(crossingTime, 0);
                vehicle.m_laps[vehicle.m_current_lap_number] = lapData;
                vehicle.m_completed_laps++;
                
                // Update best lap time and ID
                if (crossingTime < vehicle.m_best_lap_time || vehicle.m_best_lap_time < 0.0f)
                {
                    vehicle.m_best_lap_time = crossingTime;
                    vehicle.bestlapID = vehicle.m_current_lap_number; // Track which lap is best
                }
                
                std::cout << "[RACE MANAGER] Vehicle #" << vehicleID 
                          << " completed Lap " << vehicle.m_current_lap_number
                          << " in " << std::fixed << std::setprecision(3) << crossingTime << "s"
                          << " | Total completed: " << vehicle.m_completed_laps << std::endl;
                
                // Start new lap
                vehicle.m_current_lap_number++;
            }
            
            // Reset timer after crossing
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
        
        // Update previous position for next frame
        vehicle.m_prev_x = vehicle.m_normalized_x;
        vehicle.m_prev_y = vehicle.m_normalized_y;
        vehicle.m_prev_track_progress = vehicle.m_track_progress;
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
        if (currentLeader != previousLeader && previousLeader != -1)
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

// ============================================================================
// RESET SESSION (clear all lap data)
// ============================================================================
void RaceManager::ResetSession()
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    
    for (auto& [vehicleID, vehicle] : g_vehicles)
    {
        vehicle.m_laps.clear();
        vehicle.laps.clear(); // Clear telemetry samples
        vehicle.m_current_lap_timer = 0.0f;
        vehicle.m_current_lap_number = RaceConstants::LAP_START_NUMBER;
        vehicle.m_completed_laps = 0;
        vehicle.m_has_started_first_lap = false;
        vehicle.m_best_lap_time = -1.0f;
        vehicle.bestlapID = -1; // Reset best lap ID
        vehicle.m_prev_track_progress = 0.0;
        vehicle.m_total_progress = 0.0;
    }
    
    std::cout << "[RACE MANAGER] Session reset - all lap data cleared" << std::endl;
}

// ============================================================================
// GET STANDINGS (sorted leaderboard) - Internal version without mutex lock
// ============================================================================
std::vector<VehicleStanding> RaceManager::GetStandingsInternal() const
{
    std::vector<VehicleStanding> standings;
    
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        VehicleStanding standing;
        standing.vehicleID = vehicleID;
        standing.completedLaps = vehicle.m_completed_laps;
        standing.currentLapNumber = vehicle.m_current_lap_number;
        standing.currentLapTime = vehicle.m_current_lap_timer;
        standing.hasStartedFirstLap = vehicle.m_has_started_first_lap;
        standing.distanceFromStart = vehicle.m_track_progress;
        
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
        standing.deltaTimeToBest = CalculateLapTimeDiffInternal(vehicleID);
        standing.deltaTimeToLeader = CalculateLeaderTimeDiffInternal(vehicleID);
        
        standings.push_back(standing);
    }
    
    // ========================================================================
    // SORT: 1) Started racing? 2) Completed laps (desc), 3) Progress (desc)
    // ========================================================================
    std::sort(standings.begin(), standings.end(), 
        [](const VehicleStanding& a, const VehicleStanding& b) -> bool
        {
            if (a.hasStartedFirstLap != b.hasStartedFirstLap)
                return a.hasStartedFirstLap > b.hasStartedFirstLap;
            
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

float RaceManager::GetVehicleCurrentLapTime(int32_t vehicleID) const
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
        return it->second.m_current_lap_timer;
    
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
    return CalculateLapTimeDiff(vehicleID); // Thread-safe (has mutex inside)
}

float RaceManager::GetVehicleLeaderDelta(int32_t vehicleID) const
{
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
// SAVE RESULTS TO FILE (Ctrl+P)
// ============================================================================
bool RaceManager::SaveResultsToFile() const
{
    // ??????? ?????????? saves ???? ?? ???
    std::filesystem::create_directories("saves");
    
    // ?????????? ??? ????? ? timestamp
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
    
    // ?????????? ??????????
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
                int total_minutes = static_cast<int>(standing.totalRaceTime) / 60;
                float total_seconds = std::fmod(standing.totalRaceTime, 60.0f);
                file << "  Total Race Time: " << total_minutes << ":" 
                     << std::fixed << std::setprecision(3) << total_seconds << "\n";
            }
            
            if (standing.bestLapTime < 999999.0f)
            {
                int minutes = static_cast<int>(standing.bestLapTime) / 60;
                float seconds = std::fmod(standing.bestLapTime, 60.0f);
                file << "  Best Lap Time: " << minutes << ":" 
                     << std::fixed << std::setprecision(3) << seconds << "\n";
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
                    int current_minutes = static_cast<int>(vehicle.m_current_lap_timer) / 60;
                    float current_seconds = std::fmod(vehicle.m_current_lap_timer, 60.0f);
                    file << "  Current Lap " << current_lap_num << ": " << current_minutes << ":" 
                         << std::fixed << std::setprecision(3) << current_seconds << " (in progress)\n";
                }
                
                // Completed laps
                if (!vehicle.m_laps.empty())
                {
                    file << "  Lap Times:\n";
                    for (auto lap_it = vehicle.m_laps.begin(); lap_it != vehicle.m_laps.end(); ++lap_it)
                    {
                        int lap_minutes = static_cast<int>(lap_it->second.lapTime) / 60;
                        float lap_seconds = std::fmod(lap_it->second.lapTime, 60.0f);
                        file << "    Lap " << lap_it->first << ": " << lap_minutes << ":" 
                             << std::fixed << std::setprecision(3) << lap_seconds << "\n";
                    }
                }
            }
            
            file << "\n";
        }
    }
    
    file << "========================================\n";
    file << "End of Report\n";
    file << "========================================\n";
    
    file.close();
    
    std::cout << "[RACE MANAGER] Results saved to: " << filename << std::endl;
    return true;
}
