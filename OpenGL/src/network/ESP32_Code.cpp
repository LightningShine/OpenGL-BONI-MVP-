#include "../network/ESP32_Code.h"
#include "../vehicle/Vehicle.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"


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


void readFromCOM()
{
    uint32_t header = 0;
    uint8_t byte;
    // Читаем по байту, пока не соберем 4 байта
    if (serial.readBytes(&byte, 1) > 0)
    {
        header = (header << 8) | byte;

        // Проверяем, является ли текущий header одним из наших типов
        if (header == (uint32_t)PacketType::TELEMETRY) {
            TelemetryPacket Tpacket;
            serial.readBytes((char*)&Tpacket + 4, sizeof(Tpacket) - 4);
        }
        
    }
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

// Simulation worker function (runs in separate thread)
static void simulationThreadWorker(int vehicle_id, std::vector<SplinePoint> smooth_path)
{
    // Calculate cumulative distances along the path
    std::vector<float> cumulative_distances;
    cumulative_distances.reserve(smooth_path.size());
    cumulative_distances.push_back(0.0f);
    
    float total_path_length = 0.0f;
    for (size_t i = 1; i < smooth_path.size(); i++) {
        float segment_length = glm::distance(smooth_path[i].position, smooth_path[i - 1].position);
        total_path_length += segment_length;
        cumulative_distances.push_back(total_path_length);
    }
    
    if (total_path_length < 1e-6f) {
        std::cerr << "Error: Path length is zero!" << std::endl;
        return;
    }

    const float duration_seconds = SimulationConstants::DEFAULT_DURATION_SECONDS;
    const float update_interval_ms = SimulationConstants::UPDATE_INTERVAL_MS;
    const int total_updates = static_cast<int>(duration_seconds * SimulationConstants::UPDATE_RATE_HZ);

    for (int step = 0; step <= total_updates; step++) {
        float progress = static_cast<float>(step) / static_cast<float>(total_updates);
        
        // Calculate target distance along path
        float target_distance = progress * total_path_length;
        
        // Find segment containing this distance (binary search for efficiency)
        size_t segment_index = 0;
        for (size_t i = 1; i < cumulative_distances.size(); i++) {
            if (cumulative_distances[i] >= target_distance) {
                segment_index = i - 1;
                break;
            }
        }
        
        // Handle edge case (end of path)
        if (segment_index >= smooth_path.size() - 1) {
            segment_index = smooth_path.size() - 2;
        }
        
        // Calculate interpolation factor within this segment
        float segment_start_dist = cumulative_distances[segment_index];
        float segment_end_dist = cumulative_distances[segment_index + 1];
        float segment_length = segment_end_dist - segment_start_dist;
        
        float local_fraction = 0.0f;
        if (segment_length > 1e-6f) {
            local_fraction = (target_distance - segment_start_dist) / segment_length;
            local_fraction = glm::clamp(local_fraction, 0.0f, 1.0f);
        }
        
        // Interpolate position
        glm::vec2 current_pos = glm::mix(
            smooth_path[segment_index].position,
            smooth_path[segment_index + 1].position,
            local_fraction
        );

        // Denormalize (multiply by MAP_SIZE)
        double offset_x = current_pos.x * MapConstants::MAP_SIZE;
        double offset_y = current_pos.y * MapConstants::MAP_SIZE;

        // Convert to GPS coordinates (simplified approximation)
        double lat_offset = offset_y / SimulationConstants::METERS_PER_DEGREE_LAT;
        double lon_offset = offset_x / (SimulationConstants::METERS_PER_DEGREE_LAT * 
                                       std::cos(g_map_origin.m_origin_lat_dd * SimulationConstants::TWO_PI / 360.0));

        double sim_lat = g_map_origin.m_origin_lat_dd + lat_offset;
        double sim_lon = g_map_origin.m_origin_lon_dd + lon_offset;

        // Calculate realistic speed variation
        double speed_kph = SimulationConstants::MIN_SPEED_KPH + 
                          SimulationConstants::SPEED_VARIATION_KPH * 
                          std::sin(progress * SimulationConstants::TWO_PI);

        // Create telemetry packet
        TelemetryPacket packet;
        packet.ID = vehicle_id;
        packet.lat = static_cast<int32_t>(sim_lat * 1e7);
        packet.lon = static_cast<int32_t>(sim_lon * 1e7);
        packet.speed = static_cast<uint16_t>(speed_kph * 100.0);
        packet.acceleration = static_cast<int16_t>(0);
        packet.gForceX = static_cast<int16_t>(0);
        packet.gForceY = static_cast<int16_t>(0);
        packet.fixtype = 4;
        packet.time = static_cast<uint32_t>(step * update_interval_ms);

        // Update vehicle (thread-safe)
        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            g_vehicles[vehicle_id] = Vehicle(packet);
        }

        // Progress indicator
        //if (step % SimulationConstants::PROGRESS_LOG_INTERVAL == 0) {
        //    std::cout << "Vehicle #" << vehicle_id << " progress: " 
        //              << static_cast<int>(progress * 100) 
        //              << "% (distance: " << target_distance << "/" << total_path_length << ")" 
        //              << std::endl;
        //}

        // Sleep until next update
        if (step < total_updates) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(update_interval_ms)));
        }
    }

    std::cout << "Simulation complete for vehicle #" << vehicle_id << std::endl;
	removeVehicles();
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

    std::cout << "Starting vehicle simulation:" << std::endl;
    std::cout << "  Vehicle ID: " << vehicle_id << std::endl;
    std::cout << "  Duration: " << SimulationConstants::DEFAULT_DURATION_SECONDS << "s" << std::endl;
    std::cout << "  Path points: " << smooth_track_points.size() << std::endl;
    std::cout << "  Total distance: " << total_distance << " units" << std::endl;

    // Launch simulation in separate thread
    std::thread simulation_thread(simulationThreadWorker, vehicle_id, smooth_track_points);
    simulation_thread.detach();
}