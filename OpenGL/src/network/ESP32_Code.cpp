#include "../network/ESP32_Code.h"
#include "../network/Server.h"
#include "../vehicle/Vehicle.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"
#include <random>
#include <chrono>
#include <GeographicLib/UTMUPS.hpp>  // For accurate GPS conversion

// ============================================================================
// EXTERNAL GLOBALS
// ============================================================================
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;
extern MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;

serialib serial;

bool openCOMPort(const std::string& port_name)
{
    int result = serial.openDevice(port_name.c_str(), 115200);

    if (result == 1) {
        std::cout << "Successfully opened " << port_name << std::endl;

        serial.flushReceiver();

        return true;
    }

    switch (result) {
    case -1: std::cerr << "Error: Device not found: " << port_name << std::endl; break;
    case -2: std::cerr << "Error: Error while setting port parameters." << std::endl; break;
    case -3: std::cerr << "Error: Another program is already using this port!" << std::endl; break;
    default: std::cerr << "Error: Unknown error opening port." << std::endl; break;
    }
    return false;
}


// ============================================================================
// UNIFIED TELEMETRY PROCESSING - Single entry point for all data sources
// Used by: simulation, real COM port, network clients
// ============================================================================
void processIncomingTelemetry(const TelemetryPacket& packet)
{
    // ✅ 1. Update local vehicle data
    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);

        auto it = g_vehicles.find(packet.ID);

        if (it != g_vehicles.end())
        {
            // Update existing vehicle
            Vehicle& vehicle = it->second;

            vehicle.m_lat_dd = packet.lat / 1e7;
            vehicle.m_lon_dd = packet.lon / 1e7;
            vehicle.m_speed_kph = packet.speed / 100.0;
            vehicle.m_acceleration = packet.acceleration / 100.0;
            vehicle.m_g_force_x = packet.gForceX / 100.0;
            vehicle.m_g_force_y = packet.gForceY / 100.0;

            coordinatesToMeters(vehicle.m_lat_dd, vehicle.m_lon_dd, 
                               vehicle.m_meters_easting, vehicle.m_meters_northing);
            getCoordinateDifferenceFromOrigin(vehicle.m_meters_easting, vehicle.m_meters_northing,
                                             vehicle.m_normalized_x, vehicle.m_normalized_y);

            vehicle.m_last_update_time = std::chrono::steady_clock::now();
        }
        else
        {
            // Create new vehicle from packet
            g_vehicles[packet.ID] = Vehicle(packet);

            std::cout << "[TELEMETRY] New vehicle detected: ID #" << packet.ID << std::endl;
        }
    }

    // ✅ 2. Broadcast to network clients (if server is running)
    BroadcastTelemetryToClients(packet);
}

// ============================================================================
// REAL DATA CAPTURE - Worker thread for reading COM port
// ============================================================================
static void realDataThreadWorker(const std::string& com_port)
{
    if (!openCOMPort(com_port)) {
        std::cerr << "[REAL DATA] Failed to open " << com_port << std::endl;
        return;
    }

    std::cout << "[REAL DATA] Listening on " << com_port << std::endl;

    while (true)
    {
        // Read header (4 bytes)
        uint32_t header = 0;
        uint8_t byte;
        int bytesRead = 0;

        // Assemble header byte by byte
        for (int i = 0; i < 4; i++)
        {
            if (serial.readBytes(&byte, 1) > 0)
            {
                header = (header << 8) | byte;
                bytesRead++;
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                break;
            }
        }

        if (bytesRead != 4) continue;

        // Check packet type
        if (header == static_cast<uint32_t>(PacketType::TELEMETRY))
        {
            TelemetryPacket packet;
            packet.MagicMarker = header;

            // Read remaining data
            size_t dataSize = sizeof(TelemetryPacket) - sizeof(uint32_t);
            if (serial.readBytes(reinterpret_cast<char*>(&packet) + 4, dataSize) == dataSize)
            {
                // ✅ Process real packet through unified system
                processIncomingTelemetry(packet);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


// ============================================================================
// START REAL DATA CAPTURE (public API)
// ============================================================================
void startRealDataCapture(const std::string& com_port)
{
    std::thread real_thread(realDataThreadWorker, com_port);
    real_thread.detach();
}

void testSerial()
{
    if (openCOMPort("COM1"))
    {
        std::cout << "Connection established. Ready to receive data." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(2));

        serial.closeDevice();
        std::cout << "Port closed." << std::endl;
    }
}

// ============================================================================
// HELPER FUNCTIONS FOR SIMULATION (Single Responsibility)
// ============================================================================

// 1. Calculate cumulative distances along track
static std::pair<std::vector<float>, float> calculateCumulativeDistances(
    const std::vector<SplinePoint>& track)
{
    std::vector<float> cumulative_distances;
    cumulative_distances.reserve(track.size());
    cumulative_distances.push_back(0.0f);

    float total_length = 0.0f;
    for (size_t i = 1; i < track.size(); i++)
    {
        float segment_length = glm::distance(track[i].position, track[i - 1].position);
        total_length += segment_length;
        cumulative_distances.push_back(total_length);
    }

    return {cumulative_distances, total_length};
}

// 2. Update vehicle physics (speed, distance)
static void updateVehiclePhysics(
    float& currentDistance,
    double& currentSpeedKph,
    float& timeSinceLastSpeedChange,
    float& timeUntilSpeedChange,
    float deltaTime,
    float total_track_length,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& speed_dist,
    std::uniform_real_distribution<float>& duration_dist)
{
    // Speed change logic
    timeSinceLastSpeedChange += deltaTime;
    if (timeSinceLastSpeedChange >= timeUntilSpeedChange)
    {
        currentSpeedKph = speed_dist(rng);
        timeUntilSpeedChange = duration_dist(rng);
        timeSinceLastSpeedChange = 0.0f;
    }

    // Calculate distance
    double speedMetersPerSecond = (currentSpeedKph * 1000.0) / 3600.0;
    double speedNormalizedPerSecond = speedMetersPerSecond / MapConstants::MAP_SIZE;
    float distanceThisFrame = static_cast<float>(speedNormalizedPerSecond * deltaTime);

    currentDistance += distanceThisFrame;

    // Cyclic movement
    if (currentDistance >= total_track_length)
    {
        currentDistance = std::fmod(currentDistance, total_track_length);
    }
}

// 3. Interpolate position on track
static glm::vec2 interpolateTrackPosition(
    float currentDistance,
    const std::vector<SplinePoint>& track,
    const std::vector<float>& cumulative_distances)
{
    // Find segment
    size_t segment_index = 0;
    for (size_t i = 1; i < cumulative_distances.size(); i++)
    {
        if (cumulative_distances[i] >= currentDistance)
        {
            segment_index = i - 1;
            break;
        }
    }

    if (segment_index >= track.size() - 1)
    {
        segment_index = track.size() - 2;
    }

    // Local interpolation
    float segment_start = cumulative_distances[segment_index];
    float segment_end = cumulative_distances[segment_index + 1];
    float segment_length = segment_end - segment_start;

    float local_fraction = 0.0f;
    if (segment_length > 1e-6f)
    {
        local_fraction = (currentDistance - segment_start) / segment_length;
        local_fraction = glm::clamp(local_fraction, 0.0f, 1.0f);
    }

    return glm::mix(
        track[segment_index].position,
        track[segment_index + 1].position,
        local_fraction
    );
}

// 4. Convert normalized coordinates to GPS
static bool convertNormalizedToGPS(
    const glm::vec2& normalized_pos,
    double& out_latitude,
    double& out_longitude)
{
    double meters_easting = g_map_origin.m_origin_meters_easting + 
                           (normalized_pos.x * MapConstants::MAP_SIZE);
    double meters_northing = g_map_origin.m_origin_meters_northing + 
                            (normalized_pos.y * MapConstants::MAP_SIZE);

    try {
        using namespace GeographicLib;
        UTMUPS::Reverse(g_map_origin.m_origin_zone_int, true,
                       meters_easting, meters_northing,
                       out_latitude, out_longitude);
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[GPS] Conversion error: " << e.what() << std::endl;
        return false;
    }
}

// 5. Create telemetry packet
static TelemetryPacket createTelemetryPacket(
    int vehicle_id,
    double latitude,
    double longitude,
    double speed_kph,
    float track_progress)
{
    TelemetryPacket packet;
    packet.MagicMarker = static_cast<uint32_t>(PacketType::TELEMETRY);
    packet.ID = vehicle_id;
    packet.lat = static_cast<int32_t>(latitude * 1e7);
    packet.lon = static_cast<int32_t>(longitude * 1e7);
    packet.speed = static_cast<uint16_t>(speed_kph * 100.0);
    packet.acceleration = 0;
    packet.gForceX = 0;
    packet.gForceY = 0;
    packet.fixtype = 4;
    packet.time = 0;

    return packet;
}

// ============================================================================
// SIMULATION WORKER (coordinates tasks)
// ============================================================================
// Simulation worker function (runs in separate thread) - ЦИКЛИЧЕСКОЕ ДВИЖЕНИЕ
static void simulationThreadWorker(int vehicle_id, std::vector<SplinePoint> smooth_path)
{
    std::cout << "[SIM] Vehicle #" << vehicle_id << " started (track points: " << smooth_path.size() << ")" << std::endl;

    // ✅ 1. Calculate cumulative distances (once)
    auto [cumulative_distances, total_track_length] = calculateCumulativeDistances(smooth_path);

    if (total_track_length < 1e-6f)
    {
        std::cerr << "[SIM] Error: Track length is zero!" << std::endl;
        return;
    }

    std::cout << "[SIM] Vehicle #" << vehicle_id << " track length: " << total_track_length << " units" << std::endl;

    // ✅ 2. Initialize random number generator
    std::mt19937 gen(vehicle_id * 12345 + static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<double> speed_dist(20.0, 100.0);
    std::uniform_real_distribution<float> segment_duration_dist(3.0f, 8.0f);

    // ✅ 3. Initialize physics state
    double currentSpeedKph = speed_dist(gen);
    float timeUntilSpeedChange = segment_duration_dist(gen);
    float timeSinceLastSpeedChange = 0.0f;
    float currentDistance = 0.0f;

    const float update_interval_ms = SimulationConstants::UPDATE_INTERVAL_MS;
    const float deltaTime = update_interval_ms / 1000.0f;

    std::cout << "[SIM] Vehicle #" << vehicle_id << " initial speed: " << currentSpeedKph << " km/h" << std::endl;

    // ✅ 4. Main simulation loop
    while (true)
    {
        // Check if vehicle still exists
        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            if (g_vehicles.find(vehicle_id) == g_vehicles.end())
            {
                std::cout << "[SIM] Vehicle #" << vehicle_id << " removed, stopping simulation" << std::endl;
                return;
            }
        }

        // ✅ Update physics
        updateVehiclePhysics(
            currentDistance,
            currentSpeedKph,
            timeSinceLastSpeedChange,
            timeUntilSpeedChange,
            deltaTime,
            total_track_length,
            gen,
            speed_dist,
            segment_duration_dist
        );

        // ✅ Interpolate position
        glm::vec2 current_pos = interpolateTrackPosition(
            currentDistance,
            smooth_path,
            cumulative_distances
        );

        // ✅ Convert to GPS
        double sim_lat, sim_lon;
        if (!convertNormalizedToGPS(current_pos, sim_lat, sim_lon))
        {
            continue; // Skip frame on error
        }

        // ✅ Calculate track progress (for RaceManager)
        float track_progress = currentDistance / total_track_length;

        // ✅ Create telemetry packet
        TelemetryPacket packet = createTelemetryPacket(
            vehicle_id,
            sim_lat,
            sim_lon,
            currentSpeedKph,
            track_progress
        );

        // ✅ Update track progress in vehicle (for lap detection)
        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            auto it = g_vehicles.find(vehicle_id);
            if (it != g_vehicles.end())
            {
                it->second.m_track_progress = static_cast<double>(track_progress);
            }
        }

        // ✅ Process packet through unified system (same as real data!)
        processIncomingTelemetry(packet);

        // Sleep until next update
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(update_interval_ms)));
    }
}

void simulateVehicleMovement(int vehicle_id, const std::vector<SplinePoint>& smooth_track_points)
{
    // Guard clauses
    if (smooth_track_points.empty()) {
        std::cerr << "Error: Empty track points for simulation" << std::endl;
        return;
    }

    if (!g_is_map_loaded) {
        std::cerr << "Error: Map not loaded, cannot simulate vehicle movement" << std::endl;
        return;
    }

    // Calculate total path length for info
    float total_distance = 0.0f;
    for (size_t i = 1; i < smooth_track_points.size(); ++i) {
        total_distance += glm::distance(smooth_track_points[i].position, smooth_track_points[i - 1].position);
    }

    // Launch simulation in separate thread
    std::thread simulation_thread(simulationThreadWorker, vehicle_id, smooth_track_points);
    simulation_thread.detach();
}