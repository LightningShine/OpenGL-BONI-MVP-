#include "./TimeDiff.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <cmath>

// Prevent Windows.h min/max macros from interfering
#undef max
#undef min

// ============================================================================
// CALCULATE LAP TIME DIFFERENCE TO BEST LAP (INTERNAL - NO MUTEX)
// Compares current lap progress with interpolated best lap time
// Called from GetStandingsInternal where mutex is already locked
// ============================================================================
float CalculateLapTimeDiffInternal(int vehicleID)
{
    // 1. Get best lap time from vehicle by ID
    // 2. Get current lap time from vehicle by ID
    // 3. Calculate the difference between current lap time and best lap time
    // 3.1 Find current track progress from vehicle by ID
    // 3.2 Compare current track progress with the track progress at the best lap time
    // 4. Return the time difference

    if (g_vehicles.find(vehicleID) == g_vehicles.end()) return 0.0f;
    auto& vehicle = g_vehicles.at(vehicleID);

    int bestLapID = vehicle.bestlapID;

    if (bestLapID == -1 || vehicle.laps.find(bestLapID) == vehicle.laps.end()) return 0.0f;

    const auto& bestLapSamples = vehicle.laps.at(bestLapID).samples;
    if (bestLapSamples.empty()) return 0.0f;

    double currentProgress = vehicle.m_track_progress;  // Use m_track_progress (0.0-1.0)
    double currentTime = vehicle.m_current_lap_timer;

    // Binary search for closest sample at current progress
    auto it = std::lower_bound(bestLapSamples.begin(), bestLapSamples.end(), currentProgress,
        [](const LapInfo& s, double val) {
            return s.progress < val;
        });

    // Edge case: current progress beyond best lap samples
    if (it == bestLapSamples.end())
    {
        return (float)(currentTime - bestLapSamples.back().timefromstart);
    }

    // Edge case: current progress before first sample
    if (it == bestLapSamples.begin()) {
        return (float)(currentTime - it->timefromstart);
    }

    // Linear interpolation between two samples
    auto prevIt = std::prev(it);
    double t = (currentProgress - prevIt->progress) / (it->progress - prevIt->progress);
    double interpolatedBestTime = prevIt->timefromstart + (it->timefromstart - prevIt->timefromstart) * t;

    return (float)(currentTime - interpolatedBestTime);
}

// ============================================================================
// CALCULATE LAP TIME DIFFERENCE TO BEST LAP (PUBLIC - WITH MUTEX)
// Thread-safe wrapper for external calls (UI, etc.)
// ============================================================================
float CalculateLapTimeDiff(int vehicleID)
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    return CalculateLapTimeDiffInternal(vehicleID);
}

// ============================================================================
// CALCULATE TIME DIFFERENCE TO LEADER (INTERNAL - NO MUTEX)
// Finds when leader was at same total progress and calculates time gap
// Called from GetStandingsInternal where mutex is already locked
// ============================================================================
float CalculateLeaderTimeDiffInternal(int vehicleID)
{
    // 1. Get Vehicle current total lap progress by Vehicle ID
    // 2. Find when leader was at this same track progress 
    // 3. Calculate the difference between Vehicle current progress and leader progress time at the same track progress

    if (g_vehicles.find(vehicleID) == g_vehicles.end()) return 0.0f;
    auto& vehicle = g_vehicles.at(vehicleID);
    if (vehicle.m_is_leader) return 0.0f;

    // Find leader
    int leaderID = -1;
    for (auto& [id, v] : g_vehicles)
    {
        if (v.m_is_leader) {
            leaderID = id;
            break;
        }
    }
    if (leaderID == -1) return 0.0f;

    auto& leader = g_vehicles.at(leaderID);

    double targetProgress = vehicle.m_total_progress;

    // === Search for closest leader progress ===
    bool found = false;
    std::chrono::steady_clock::time_point leaderTime;
    double bestDiff = std::numeric_limits<double>::max();

    for (auto& [lapNum, lap] : leader.laps)
    {
        for (auto& sample : lap.samples)
        {
            double diff = std::abs(sample.total_progress - targetProgress);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                leaderTime = sample.timestamp;
                found = true;
            }
        }
    }

    if (!found) return 0.0f;

    // === Calculate time difference ===
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> delta = now - leaderTime;

    return delta.count(); // seconds
}

// ============================================================================
// CALCULATE TIME DIFFERENCE TO LEADER (PUBLIC - WITH MUTEX)
// Thread-safe wrapper for external calls (UI, etc.)
// ============================================================================
float CalculateLeaderTimeDiff(int vehicleID)
{
    std::lock_guard<std::mutex> lock(g_vehicles_mutex);
    return CalculateLeaderTimeDiffInternal(vehicleID);
}
