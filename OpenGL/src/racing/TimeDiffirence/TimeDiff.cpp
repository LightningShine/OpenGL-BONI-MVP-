#include "./TimeDiff.h"
#include "../../rendering/Interpolation.h"
#include "../../Config.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <unordered_map>

// ============================================================================
// DEBUG: Uncomment to enable detailed time diff logging
// ============================================================================
//#define DEBUG_TIME_DIFF

// Prevent Windows.h min/max macros from interfering
#undef max
#undef min

extern std::vector<SplinePoint> g_smooth_track_points;

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
// TRACK LENGTH CACHE
// Computes total spline length in normalized units once and caches it.
// g_smooth_track_points uses normalized coordinates (same as m_track_progress),
// so the result is in the same unit space. Multiply by a meters-per-unit scale
// to convert to real-world meters when needed.
// ============================================================================
float GetCachedTrackLengthMeters()
{
    static float s_cachedLength = 0.0f;
    static size_t s_cachedPointCount = 0;

    if (g_smooth_track_points.size() == s_cachedPointCount && s_cachedLength > 0.0f)
        return s_cachedLength;

    s_cachedPointCount = g_smooth_track_points.size();
    if (s_cachedPointCount < 2)
    {
        s_cachedLength = 0.0f;
        return 0.0f;
    }

    float total = 0.0f;
    for (size_t i = 1; i < s_cachedPointCount; ++i)
    {
        glm::vec2 d = g_smooth_track_points[i].position - g_smooth_track_points[i - 1].position;
        total += std::sqrt(d.x * d.x + d.y * d.y);
    }
    // Close the loop
    {
        glm::vec2 d = g_smooth_track_points[0].position - g_smooth_track_points[s_cachedPointCount - 1].position;
        total += std::sqrt(d.x * d.x + d.y * d.y);
    }

    s_cachedLength = total;
    return s_cachedLength;
}

// ============================================================================
// CALCULATE TIME DIFFERENCE TO LEADER (INTERNAL - NO MUTEX)
//
// Algorithm:
//   gap_time = progressGap × trackLengthMeters / referenceSpeedMs
//
// Reference speed priority:
//   1. trackLength / leaderBestLapTime  — immune to instantaneous braking,
//      changes only when a new best lap is recorded (F1/WEC standard approach).
//   2. EMA-smoothed leader speed        — fallback when no best lap exists yet,
//      ~2 s time constant prevents gap collapse on hard braking.
//
// Using a single reference speed for ALL cars guarantees monotonic gaps:
// a car further behind always shows a strictly larger gap than one ahead.
// ============================================================================
float CalculateLeaderTimeDiffInternal(int vehicleID)
{
    if (g_vehicles.find(vehicleID) == g_vehicles.end()) return 0.0f;
    auto& vehicle = g_vehicles.at(vehicleID);

    // -----------------------------------------------------------------------
    // Find leader by highest total_progress — NOT by m_is_leader flag.
    // m_is_leader is updated AFTER GetStandingsInternal() runs, so on a lap
    // boundary it can be stale for one frame and return 0 for everyone.
    // -----------------------------------------------------------------------
    int leaderID = -1;
    double leaderProgress = -1.0;
    for (auto& [id, v] : g_vehicles)
    {
        if (v.m_has_started_first_lap && v.m_total_progress > leaderProgress)
        {
            leaderProgress = v.m_total_progress;
            leaderID = id;
        }
    }
    if (leaderID == -1 || leaderID == vehicleID) return 0.0f;

    auto& leader = g_vehicles.at(leaderID);

    // -----------------------------------------------------------------------
    // 1. Progress gap (dimensionless, 1.0 = one full lap ahead)
    // -----------------------------------------------------------------------
    double progressGap = leader.m_total_progress - vehicle.m_total_progress;
    if (progressGap <= 0.0) return 0.0f;

    // -----------------------------------------------------------------------
    // 2. Convert progress gap to real meters.
    //    GetCachedTrackLengthMeters() returns spline length in NORMALIZED units.
    //    MAP_SIZE = 100 means 1 normalized unit = 100 meters.
    // -----------------------------------------------------------------------
    float trackLengthNorm = GetCachedTrackLengthMeters();
    if (trackLengthNorm <= 0.0f) return 0.0f;
    const float trackLengthMeters = trackLengthNorm * static_cast<float>(MapConstants::MAP_SIZE);

    // -----------------------------------------------------------------------
    // 3. Reference speed for gap calculation.
    //
    // Professional approach (F1/WEC): use trackLength / bestLapTime as the
    // reference speed. This value only changes when a new best lap is set,
    // so hard braking by the leader has ZERO effect on the displayed gaps.
    //
    // Fallback (no best lap yet): EMA-smoothed speed with a long time constant
    // so that instantaneous deceleration does not collapse the gap display.
    // -----------------------------------------------------------------------
    constexpr float kMinSpeedKph = 10.0f; // absolute floor to avoid div-by-zero

    float referenceSpeedMs = 0.0f;

    // Primary: theoretical lap pace (most stable, immune to braking)
    const float leaderBestLap = leader.m_best_lap_time;
    if (leaderBestLap > 0.0f)
    {
        referenceSpeedMs = trackLengthMeters / leaderBestLap;
    }
    else
    {
        // Fallback: EMA-smoothed leader speed.
        // alpha = 0.05 → ~20-sample time constant at 10 Hz ≈ 2 seconds of smoothing.
        // Stored per leaderID so it survives across calls.
        constexpr float kEmaAlpha = 0.05f;

        static std::unordered_map<int, float> s_emaSpeed;

        float rawKph = static_cast<float>(leader.m_speed_kph);
        auto emaIt = s_emaSpeed.find(leaderID);
        if (emaIt == s_emaSpeed.end())
        {
            s_emaSpeed[leaderID] = rawKph;
            emaIt = s_emaSpeed.find(leaderID);
        }
        else
        {
            emaIt->second = emaIt->second + kEmaAlpha * (rawKph - emaIt->second);
        }

        float smoothedKph = emaIt->second;
        if (smoothedKph < kMinSpeedKph)
            smoothedKph = kMinSpeedKph;

        referenceSpeedMs = smoothedKph / 3.6f;
    }

    const float gapMeters = static_cast<float>(progressGap) * trackLengthMeters;
    float gapSeconds      = gapMeters / referenceSpeedMs;

    #ifdef DEBUG_TIME_DIFF
    std::cout << "[LEADER DIFF] veh#" << vehicleID
              << " gap=" << std::fixed << std::setprecision(4) << progressGap
              << " gapM=" << gapMeters
              << " leaderKph=" << leaderSpeedKph
              << " result=" << std::setprecision(3) << gapSeconds << "s" << std::endl;
    #endif

    return gapSeconds;
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
