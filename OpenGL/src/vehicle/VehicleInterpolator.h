#pragma once

#include <map>
#include <deque>
#include <mutex>
#include <chrono>
#include <cstdint>

// ============================================================================
// VEHICLE SNAPSHOT - Single state sample from network
// ============================================================================
struct VehicleSnapshot
{
    double timestamp;           // Server time in seconds
    double x, y;                // Normalized position
    double speed_kph;           // Speed in km/h
    double heading;             // Direction angle in radians
    double track_progress;      // Progress along track (0.0 - 1.0)
    
    VehicleSnapshot() 
        : timestamp(0.0), x(0.0), y(0.0), speed_kph(0.0), 
          heading(0.0), track_progress(0.0) {}
};

// ============================================================================
// VEHICLE INTERPOLATOR - Jitter Buffer + Client-Side Prediction
// Thread-safe singleton for smooth vehicle rendering
// ============================================================================
class VehicleInterpolator
{
public:
    // Singleton access
    static VehicleInterpolator& Get();
    
    // Add new snapshot from network (called from processIncomingTelemetry)
    void AddSnapshot(int32_t vehicleID, const VehicleSnapshot& snapshot);
    
    // Get interpolated state for rendering (called from renderAllVehicles)
    // Returns false if not enough data for interpolation
    bool GetInterpolatedState(
        int32_t vehicleID, 
        double renderTime,
        double& out_x, 
        double& out_y, 
        double& out_heading,
        double& out_speed
    );
    
    // Remove vehicle from interpolator (called when vehicle disconnects)
    void RemoveVehicle(int32_t vehicleID);
    
    // Clear all data (called on disconnect/reset)
    void Clear();
    
    // Get current time in seconds
    static double GetTime();
    
private:
    // Private constructor for singleton
    VehicleInterpolator();
    ~VehicleInterpolator();
    
    // Prevent copying
    VehicleInterpolator(const VehicleInterpolator&) = delete;
    VehicleInterpolator& operator=(const VehicleInterpolator&) = delete;
    
    // ========================================================================
    // CONFIGURATION
    // ========================================================================
    static constexpr size_t MAX_BUFFER_SIZE = 20;          // Keep last 20 snapshots
    static constexpr double INTERPOLATION_DELAY = 0.033;   // Small buffer for high-rate simulation packets
    static constexpr double EXTRAPOLATION_LIMIT = 0.050;   // Short prediction window before freezing
    
    // ========================================================================
    // PER-VEHICLE BUFFER
    // ========================================================================
    struct VehicleBuffer
    {
        std::deque<VehicleSnapshot> snapshots;
        std::mutex mutex;
        
        // Get two snapshots for interpolation at given time
        bool GetBracketingSnapshots(
            double time, 
            VehicleSnapshot& out_before, 
            VehicleSnapshot& out_after
        ) const;
        
        // Remove old snapshots to prevent memory growth
        void Cleanup(double currentTime);
    };
    
    // ========================================================================
    // DATA
    // ========================================================================
    std::map<int32_t, VehicleBuffer> m_vehicle_buffers;
    std::mutex m_buffers_mutex;  // Protects m_vehicle_buffers map
    
    // ========================================================================
    // INTERPOLATION LOGIC
    // ========================================================================
    // Linear interpolation between two snapshots
    static void Interpolate(
        const VehicleSnapshot& before,
        const VehicleSnapshot& after,
        double alpha,  // 0.0 = before, 1.0 = after
        double& out_x,
        double& out_y,
        double& out_heading,
        double& out_speed
    );
    
    // Extrapolate (predict) beyond last known snapshot
    static void Extrapolate(
        const VehicleSnapshot& last,
        double deltaTime,
        double& out_x,
        double& out_y,
        double& out_heading,
        double& out_speed
    );
};
