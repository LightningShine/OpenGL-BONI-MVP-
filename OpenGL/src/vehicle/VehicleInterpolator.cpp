#include "VehicleInterpolator.h"
#include "../Config.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// SINGLETON
// ============================================================================
VehicleInterpolator& VehicleInterpolator::Get()
{
    static VehicleInterpolator instance;
    return instance;
}

VehicleInterpolator::VehicleInterpolator()
{
    std::cout << "[INTERPOLATOR] Initialized" << std::endl;
}

VehicleInterpolator::~VehicleInterpolator()
{
    std::cout << "[INTERPOLATOR] Destroyed" << std::endl;
}

// ============================================================================
// TIME UTILITIES
// ============================================================================
double VehicleInterpolator::GetTime()
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    auto duration_val = now.time_since_epoch();
    return duration_cast<std::chrono::duration<double>>(duration_val).count();
}

// ============================================================================
// ADD SNAPSHOT FROM NETWORK
// ============================================================================
void VehicleInterpolator::AddSnapshot(int32_t vehicleID, const VehicleSnapshot& snapshot)
{
    // Get or create buffer for this vehicle
    VehicleBuffer* buffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_buffers_mutex);
        buffer = &m_vehicle_buffers[vehicleID];
    }
    
    // Thread-safe add to buffer
    std::lock_guard<std::mutex> lock(buffer->mutex);
    
    // Check for duplicate/old timestamp
    if (!buffer->snapshots.empty() && snapshot.timestamp <= buffer->snapshots.back().timestamp) {
        return;  // Ignore out-of-order or duplicate packets
    }
    
    // Add snapshot
    buffer->snapshots.push_back(snapshot);
    
    // Cleanup old snapshots (keep last MAX_BUFFER_SIZE)
    if (buffer->snapshots.size() > MAX_BUFFER_SIZE) {
        buffer->snapshots.pop_front();
    }
}

// ============================================================================
// GET INTERPOLATED STATE FOR RENDERING
// ============================================================================
bool VehicleInterpolator::GetInterpolatedState(
    int32_t vehicleID,
    double renderTime,
    double& out_x,
    double& out_y,
    double& out_heading,
    double& out_speed)
{
    // Find vehicle buffer
    VehicleBuffer* buffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_buffers_mutex);
        auto it = m_vehicle_buffers.find(vehicleID);
        if (it == m_vehicle_buffers.end()) {
            return false;  // Vehicle not found
        }
        buffer = &it->second;
    }
    
    std::lock_guard<std::mutex> lock(buffer->mutex);
    
    // Need at least 2 snapshots for interpolation
    if (buffer->snapshots.size() < 2) {
        return false;
    }
    
    // Apply interpolation delay (render behind server for smooth playback)
    double interpolationTime = renderTime - INTERPOLATION_DELAY;
    
    // Try to find bracketing snapshots
    VehicleSnapshot before, after;
    if (buffer->GetBracketingSnapshots(interpolationTime, before, after))
    {
        // Interpolate between before and after
        double timeDelta = after.timestamp - before.timestamp;
        double alpha = (timeDelta > 0.0) 
            ? (interpolationTime - before.timestamp) / timeDelta 
            : 0.0;
        
        // Clamp alpha to [0, 1]
        alpha = std::clamp(alpha, 0.0, 1.0);
        
        Interpolate(before, after, alpha, out_x, out_y, out_heading, out_speed);
        return true;
    }
    else
    {
        // No bracketing snapshots - need to extrapolate
        const VehicleSnapshot& last = buffer->snapshots.back();
        double deltaTime = interpolationTime - last.timestamp;
        
        // Only extrapolate for short duration (avoid wild predictions)
        if (deltaTime > EXTRAPOLATION_LIMIT) {
            deltaTime = EXTRAPOLATION_LIMIT;
        }
        
        // Don't extrapolate backwards
        if (deltaTime < 0.0) {
            deltaTime = 0.0;
        }
        
        Extrapolate(last, deltaTime, out_x, out_y, out_heading, out_speed);
        return true;
    }
}

// ============================================================================
// GET BRACKETING SNAPSHOTS
// ============================================================================
bool VehicleInterpolator::VehicleBuffer::GetBracketingSnapshots(
    double time,
    VehicleSnapshot& out_before,
    VehicleSnapshot& out_after) const
{
    if (snapshots.size() < 2) {
        return false;
    }
    
    // Find first snapshot after time
    auto it = std::find_if(snapshots.begin(), snapshots.end(),
        [time](const VehicleSnapshot& s) { return s.timestamp > time; });
    
    // Check if we found bracketing snapshots
    if (it != snapshots.end() && it != snapshots.begin())
    {
        out_after = *it;
        out_before = *(it - 1);
        return true;
    }
    
    return false;  // Time is outside buffer range
}

// ============================================================================
// CLEANUP OLD SNAPSHOTS
// ============================================================================
void VehicleInterpolator::VehicleBuffer::Cleanup(double currentTime)
{
    // Remove snapshots older than 1 second
    double cutoffTime = currentTime - 1.0;
    
    while (!snapshots.empty() && snapshots.front().timestamp < cutoffTime)
    {
        snapshots.pop_front();
    }
}

// ============================================================================
// LINEAR INTERPOLATION
// ============================================================================
void VehicleInterpolator::Interpolate(
    const VehicleSnapshot& before,
    const VehicleSnapshot& after,
    double alpha,
    double& out_x,
    double& out_y,
    double& out_heading,
    double& out_speed)
{
    // Linear interpolation for position
    out_x = before.x + (after.x - before.x) * alpha;
    out_y = before.y + (after.y - before.y) * alpha;
    
    // Linear interpolation for speed
    out_speed = before.speed_kph + (after.speed_kph - before.speed_kph) * alpha;
    
    // Angle interpolation (handle wraparound)
    double angleDiff = after.heading - before.heading;
    
    // Normalize to [-PI, PI]
    while (angleDiff > M_PI) angleDiff -= 2.0 * M_PI;
    while (angleDiff < -M_PI) angleDiff += 2.0 * M_PI;
    
    out_heading = before.heading + angleDiff * alpha;
}

// ============================================================================
// EXTRAPOLATION (CLIENT-SIDE PREDICTION)
// ============================================================================
void VehicleInterpolator::Extrapolate(
    const VehicleSnapshot& last,
    double deltaTime,
    double& out_x,
    double& out_y,
    double& out_heading,
    double& out_speed)
{
    // Convert speed to meters per second
    double speed_ms = (last.speed_kph / 3.6);  // km/h to m/s
    
    // Convert to normalized units (MapConstants::MAP_SIZE meters = 1.0 units)
    double speed_normalized = speed_ms / MapConstants::MAP_SIZE;
    
    // Predict position based on last known heading and speed
    double distance = speed_normalized * deltaTime;
    
    out_x = last.x + std::cos(last.heading) * distance;
    out_y = last.y + std::sin(last.heading) * distance;
    out_heading = last.heading;  // Assume constant heading
    out_speed = last.speed_kph;  // Assume constant speed
}

// ============================================================================
// REMOVE VEHICLE
// ============================================================================
void VehicleInterpolator::RemoveVehicle(int32_t vehicleID)
{
    std::lock_guard<std::mutex> lock(m_buffers_mutex);
    m_vehicle_buffers.erase(vehicleID);
}

// ============================================================================
// CLEAR ALL
// ============================================================================
void VehicleInterpolator::Clear()
{
    std::lock_guard<std::mutex> lock(m_buffers_mutex);
    m_vehicle_buffers.clear();
    std::cout << "[INTERPOLATOR] Cleared all buffers" << std::endl;
}
