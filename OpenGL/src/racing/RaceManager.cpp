#include "RaceManager.h"
#include "../vehicle/Vehicle.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"
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
// CORE UPDATE LOOP
// ============================================================================
void RaceManager::Update(float deltaTime)
{
    if (!g_is_map_loaded || !m_lineInitialized)
        return;
    
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    
    // Iterate over all vehicles
    for (auto& [vehicleID, vehicle] : g_vehicles)
    {
        // Get or create timing data for this vehicle
        VehicleTimingData& timing = m_vehicleTimings[vehicleID];
        
        // ? ?????????????? prevX/prevY ??? ?????? ????????
        if (!timing.isInitialized)
        {
            timing.prevX = vehicle.m_normalized_x;
            timing.prevY = vehicle.m_normalized_y;
            timing.isInitialized = true;
            std::cout << "[RACE MANAGER] Initialized timing data for Vehicle #" << vehicleID 
                      << " at position (" << timing.prevX << ", " << timing.prevY << ")" << std::endl;
        }
        
        // Get current position
        double currentX = vehicle.m_normalized_x;
        double currentY = vehicle.m_normalized_y;
        
        // Check for line crossing (if we have previous position)
        if (timing.hasStartedFirstLap)
        {
            // ? ????? ????????: ??????????? ??????? ????????? (0.999 -> 0.001)
            // ??? ???????? ??? ?????? ????????? ????, ???? ???? ????????????? ?? ????????? ?????
            double currentProgress = vehicle.m_track_progress;
            bool progressCycled = false;
            
            if (timing.prevProgress > 0.85 && currentProgress < 0.15)
            {
                // ???????? ??????? ????? 0 (?????????? ?????)
                progressCycled = true;
            }
            
            // ????????? ?????????????? ??????????? ?????
            float intersectionRatio = 0.0f;
            bool crossed = CheckLineSegmentIntersection(
                glm::vec2(timing.prevX, timing.prevY),
                glm::vec2(currentX, currentY),
                m_startFinishP1,
                m_startFinishP2,
                intersectionRatio
            );
            
            // ? ??????????? ???? ???? ???? ????????? ?????, ???? ???????? ??????????
            if (crossed || progressCycled)
            {
                // ============================================================
                // SUB-FRAME ACCURATE TIMING
                // ============================================================
                // Calculate exact time of crossing:
                // If ratio = 0.3, the crossing happened 30% through this frame
                float crossingTime = timing.currentLapTimer + (deltaTime * intersectionRatio);
                
                // ✅ Записываем круг если он валидный (больше минимального времени)
                // ВАЖНО: Используем 0.1s как минимум (защита от ложных пересечений на старте)
                const float MIN_VALID_LAP_TIME = 0.1f;  // 100ms минимум
                
                if (crossingTime > MIN_VALID_LAP_TIME)
                {
                    // Store completed lap
                    LapData lapData(crossingTime, 0);  // Position calculated later
                    timing.laps[timing.currentLapNumber] = lapData;
                    timing.completedLaps++;
                    
                    // Update best lap time
                    if (crossingTime < timing.bestLapTime || timing.bestLapTime < 0.0f)
                    {
                        timing.bestLapTime = crossingTime;
                    }
                    
                    std::cout << "[RACE MANAGER] Vehicle #" << vehicleID 
                              << " completed Lap " << timing.currentLapNumber
                              << " in " << crossingTime << " seconds"
                              << " | Total completed laps: " << timing.completedLaps << std::endl;
                    
                    // Start new lap
                    timing.currentLapNumber++;
                }
                
                // Reset timer after crossing
                timing.currentLapTimer = deltaTime * (1.0f - intersectionRatio);  // Remaining time after crossing
            }
            else
            {
                // No crossing - just accumulate time
                timing.currentLapTimer += deltaTime;
            }
            
            // ? ????????? ??????? ???????? ??? ?????????? ?????
            timing.prevProgress = currentProgress;
        }
        else
        {
            // ================================================================
            // FIRST LAP START DETECTION
            // ================================================================
            float intersectionRatio = 0.0f;
            bool crossed = CheckLineSegmentIntersection(
                glm::vec2(timing.prevX, timing.prevY),
                glm::vec2(currentX, currentY),
                m_startFinishP1,
                m_startFinishP2,
                intersectionRatio
            );
            
            if (crossed)
            {
                // ????????? ??? ??? ?? ?????? ??????????? ??? ??????
                // (???? ?????? ????????? ?? ?????, ?????? ???????? ?? ?????? ????????? ???????)
                float timeSinceCreation = timing.currentLapTimer;
                
                if (timeSinceCreation > 0.5f)  // ??????? 0.5 ??????? ? ??????? ????????
                {
                    timing.hasStartedFirstLap = true;
                    timing.currentLapTimer = deltaTime * (1.0f - intersectionRatio);  // Start timer at crossing point
                    
                    // ✅ Инициализируем prevProgress чтобы избежать ложного циклического сброса
                    timing.prevProgress = vehicle.m_track_progress;
                    
                    // ✅ Универсальное сообщение учитывающее LAP_START_NUMBER
                    std::cout << "[RACE MANAGER] Vehicle #" << vehicleID 
                              << " crossed start/finish line, starting Lap " << timing.currentLapNumber 
                              << " (offset: " << timing.currentLapTimer << "s)" << std::endl;
                }
                else
                {
                    // ? ?????????? ??????????? ????? ?? ?????????? ?????? 0.5 ???????
                    timing.currentLapTimer += deltaTime;
                }
            }
            else
            {
                // ? ?????? ?? ????????? ????? - ??????????? ????? ? ??????? ????????
                timing.currentLapTimer += deltaTime;
            }
        }
        
        // Update previous position for next frame
        timing.prevX = currentX;
        timing.prevY = currentY;
    }
    
    // Clean up timing data for removed vehicles
    for (auto it = m_vehicleTimings.begin(); it != m_vehicleTimings.end(); )
    {
        if (g_vehicles.find(it->first) == g_vehicles.end())
        {
            std::cout << "[RACE MANAGER] Removed timing data for vehicle #" << it->first << std::endl;
            it = m_vehicleTimings.erase(it);
        }
        else
        {
            ++it;
        }
    }
    
    // ? ????????? ???? ?????? ????????? ?? ?? ??????? ??? ? SaveResults
    std::vector<VehicleStanding> standings = GetStandingsInternal();
    
    if (!standings.empty())
    {
        // ?????????? ??????
        static int32_t previousLeader = -1;
        int32_t currentLeader = standings[0].vehicleID;
        
        // ?????????? ???? ? ????
        for (auto& [id, vehicle] : g_vehicles)
        {
            vehicle.m_is_leader = false;
        }
        
        // ????????????? ???? ??????
        auto leader_it = g_vehicles.find(currentLeader);
        if (leader_it != g_vehicles.end())
        {
            leader_it->second.m_is_leader = true;
        }
        
        // DEBUG: ??????? ??? ????? ??????
        if (currentLeader != previousLeader && previousLeader != -1)
        {
            std::cout << "\n[LEADER CHANGE] New leader: Vehicle #" << currentLeader 
                      << " | Laps: " << standings[0].completedLaps 
                      << " | Progress: " << std::fixed << std::setprecision(3) << standings[0].distanceFromStart
                      << " (was Vehicle #" << previousLeader << ")" << std::endl;
            
            // ?????????? ???-3
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
    m_vehicleTimings.clear();
    std::cout << "[RACE MANAGER] Session reset - all lap data cleared" << std::endl;
}

// ============================================================================
// GET STANDINGS (sorted leaderboard) - ?????????? ?????? ??? ??????????
// ?????????? ????? ??????? ??? ???????????? (?? Update)
// ============================================================================
std::vector<VehicleStanding> RaceManager::GetStandingsInternal() const
{
    std::vector<VehicleStanding> standings;
    
    // Build standings list
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        VehicleStanding standing;
        standing.vehicleID = vehicleID;
        
        // Get timing data if exists
        auto it = m_vehicleTimings.find(vehicleID);
        if (it != m_vehicleTimings.end())
        {
            const VehicleTimingData& timing = it->second;
            standing.completedLaps = timing.completedLaps;
            standing.currentLapNumber = timing.currentLapNumber;
            standing.currentLapTime = timing.currentLapTimer;
            
            // ✅ Best lap логика зависит от LAP_START_NUMBER
            int minCompletedLaps = (RaceConstants::LAP_START_NUMBER == 0) ? 1 : 2;
            standing.bestLapTime = (timing.completedLaps >= minCompletedLaps) ? timing.bestLapTime : -1.0f;
            
            standing.hasStartedFirstLap = timing.hasStartedFirstLap; // ?
            
            // Calculate total race time (sum of all completed laps ONLY)
            standing.totalRaceTime = 0.0f;
            for (const auto& [lapNum, lapData] : timing.laps)
            {
                standing.totalRaceTime += lapData.lapTime;
            }
            // ?? ????????? ??????? ???? - ?????? ???????????!
        }
        else
        {
            standing.completedLaps = 0;
            standing.currentLapTime = 0.0f;
            standing.bestLapTime = -1.0f;  // -1 = no data yet
            standing.totalRaceTime = 0.0f;
            standing.hasStartedFirstLap = false; // ?
        }
        
        // Calculate distance from start
        standing.distanceFromStart = CalculateDistanceFromStart(vehicle);
        
        standings.push_back(standing);
    }
    
    // ========================================================================
    // SORT: 1) Started racing? 2) Completed laps (desc), 3) Progress (desc)
    // ========================================================================
    std::sort(standings.begin(), standings.end(), 
        [](const VehicleStanding& a, const VehicleStanding& b) -> bool
        {
            // ? ?????: ??????, ??????? ?? ?????? ?????, ?????? ??????
            if (a.hasStartedFirstLap != b.hasStartedFirstLap)
                return a.hasStartedFirstLap > b.hasStartedFirstLap;
            
            // ?????? ????????: ?????? ?????? = ???? ???????
            if (a.completedLaps != b.completedLaps)
                return a.completedLaps > b.completedLaps;
            
            // ?????? ????????: ?????? ???????? ?? ??????? ????? = ???? ???????
            // (??? ?????? ??????????? ?? ??????? ????? - ??? ???????)
            return a.distanceFromStart > b.distanceFromStart;
        }
    );
    
    // ========================================================================
    // ASSIGN POSITIONS & DETECT LAPPED CARS (??? ?????????? ?????? ??????!)
    // ????? ?????? ??????????? ????? UpdateLeaderFlags() ? Update()
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
    
    for (const auto& [vehicleID, timing] : m_vehicleTimings)
    {
        if (timing.completedLaps > maxLaps)
            maxLaps = timing.completedLaps;
    }
    
    return maxLaps;
}

// ============================================================================
// LAP DATA ACCESS (for UI)
// ============================================================================
const std::map<int, LapData>* RaceManager::GetVehicleLaps(int32_t vehicleID) const
{
    auto it = m_vehicleTimings.find(vehicleID);
    if (it != m_vehicleTimings.end())
        return &(it->second.laps);
    
    return nullptr;
}

float RaceManager::GetVehicleCurrentLapTime(int32_t vehicleID) const
{
    auto it = m_vehicleTimings.find(vehicleID);
    if (it != m_vehicleTimings.end())
        return it->second.currentLapTimer;
    
    return 0.0f;
}

int RaceManager::GetVehicleCompletedLaps(int32_t vehicleID) const
{
    auto it = m_vehicleTimings.find(vehicleID);
    if (it != m_vehicleTimings.end())
        return it->second.completedLaps;
    
    return 0;
}

float RaceManager::GetVehicleBestLapTime(int32_t vehicleID) const
{
    auto it = m_vehicleTimings.find(vehicleID);
    if (it != m_vehicleTimings.end())
    {
        
        int minCompletedLaps = (RaceConstants::LAP_START_NUMBER == 0) ? 1 : 2;
        
        if (it->second.completedLaps < minCompletedLaps)
            return -1.0f;  // Показываем черточки
        
        return it->second.bestLapTime;
    }
    
    return -1.0f;  // -1 = no data yet
}

// ============================================================================
// ✅ NEW: LAP TIMER DATA ACCESS
// ============================================================================
float RaceManager::GetVehiclePreviousLapTime(int32_t vehicleID) const
{
    auto it = m_vehicleTimings.find(vehicleID);
    if (it != m_vehicleTimings.end())
    {
        const auto& timing = it->second;
        
        // ✅ Предыдущий круг = текущий номер - 1
        // Проверяем что предыдущий круг существует (не уходим в отрицательные)
        int previousLapNumber = timing.currentLapNumber - 1;
        
        // Защита: не ищем круги с номером меньше LAP_START_NUMBER
        if (previousLapNumber < RaceConstants::LAP_START_NUMBER)
            return -1.0f;  // Нет предыдущего круга
        
        auto lapIt = timing.laps.find(previousLapNumber);
        if (lapIt != timing.laps.end())
            return lapIt->second.lapTime;
    }
    
    return -1.0f;  // -1 означает нет данных
}

float RaceManager::GetVehicleDeltaTime(int32_t vehicleID) const
{
    auto it = m_vehicleTimings.find(vehicleID);
    if (it != m_vehicleTimings.end())
    {
        const auto& timing = it->second;
        float currentTime = timing.currentLapTimer;
        float compareTime = -1.0f;
        
        // Определяем с каким кругом сравнивать (из Config.h)
        if (RaceConstants::LAP_DELTA_COMPARE_MODE == -1)
        {
            // Сравнение с лучшим кругом
            compareTime = timing.bestLapTime;
        }
        else if (RaceConstants::LAP_DELTA_COMPARE_MODE == 0)
        {
            // Сравнение с предыдущим кругом
            int previousLapNumber = timing.currentLapNumber - 1;
            auto lapIt = timing.laps.find(previousLapNumber);
            if (lapIt != timing.laps.end())
                compareTime = lapIt->second.lapTime;
        }
        else
        {
            // Сравнение с конкретным кругом
            int specificLap = RaceConstants::LAP_DELTA_COMPARE_MODE;
            auto lapIt = timing.laps.find(specificLap);
            if (lapIt != timing.laps.end())
                compareTime = lapIt->second.lapTime;
        }
        
        // Если нет данных для сравнения, возвращаем 0
        if (compareTime < 0.0f)
            return 0.0f;
        
        // Разница: текущее время - эталонное время
        // Положительное = медленнее, отрицательное = быстрее
        return currentTime - compareTime;
    }
    
    return 0.0f;
}

int RaceManager::GetVehicleCurrentLapNumber(int32_t vehicleID) const
{
    auto it = m_vehicleTimings.find(vehicleID);
    if (it != m_vehicleTimings.end())
        return it->second.currentLapNumber;
    
    return 0;
}

// ============================================================================
// DEBUG: PRINT SESSION SUMMARY
// ============================================================================
void RaceManager::PrintSessionSummary() const
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "       RACE SESSION SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& [vehicleID, timing] : m_vehicleTimings)
    {
        std::cout << "\nVehicle #" << vehicleID << ":" << std::endl;
        std::cout << "  Completed Laps: " << timing.completedLaps << std::endl;
        std::cout << "  Current Lap Time: " << timing.currentLapTimer << "s" << std::endl;
        std::cout << "  Best Lap: " << timing.bestLapTime << "s" << std::endl;
        
        std::cout << "  Lap Times:" << std::endl;
        for (auto it = timing.laps.begin(); it != timing.laps.end(); ++it)
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
            
            // ????????? ??????? ??????
            auto it = m_vehicleTimings.find(standing.vehicleID);
            if (it != m_vehicleTimings.end())
            {
                // ?????????? ??????? (?????????????) ????
                if (it->second.currentLapTimer > 0.0f)
                {
                    int current_lap_num = it->second.currentLapNumber;
                    int current_minutes = static_cast<int>(it->second.currentLapTimer) / 60;
                    float current_seconds = std::fmod(it->second.currentLapTimer, 60.0f);
                    file << "  Current Lap " << current_lap_num << ": " << current_minutes << ":" 
                         << std::fixed << std::setprecision(3) << current_seconds << " (in progress)\n";
                }
                
                // ??????????? ?????
                if (!it->second.laps.empty())
                {
                    file << "  Lap Times:\n";
                    for (auto lap_it = it->second.laps.begin(); lap_it != it->second.laps.end(); ++lap_it)
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
