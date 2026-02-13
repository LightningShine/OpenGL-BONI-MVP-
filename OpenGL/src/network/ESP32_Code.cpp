#include "../network/ESP32_Code.h"
#include "../vehicle/Vehicle.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"
#include <random>
#include <chrono>
#include <GeographicLib/UTMUPS.hpp>  // For accurate GPS conversion


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

// Simulation worker function (runs in separate thread) - ЦИКЛИЧЕСКОЕ ДВИЖЕНИЕ
static void simulationThreadWorker(int vehicle_id, std::vector<SplinePoint> smooth_path)
{
    std::cout << "[SIM] Vehicle #" << vehicle_id << " started (track points: " << smooth_path.size() << ")" << std::endl;

    // ============================================================================
    // ВЫЧИСЛЯЕМ КУМУЛЯТИВНЫЕ ДИСТАНЦИИ (расстояние от старта до каждой точки)
    // ============================================================================
    std::vector<float> cumulative_distances;
    cumulative_distances.reserve(smooth_path.size());
    cumulative_distances.push_back(0.0f);
    
    float total_track_length = 0.0f;
    for (size_t i = 1; i < smooth_path.size(); i++)
    {
        float segment_length = glm::distance(smooth_path[i].position, smooth_path[i - 1].position);
        total_track_length += segment_length;
        cumulative_distances.push_back(total_track_length);
    }
    
    if (total_track_length < 1e-6f)
    {
        std::cerr << "[SIM] Error: Track length is zero!" << std::endl;
        return;
    }
    
    std::cout << "[SIM] Vehicle #" << vehicle_id << " track length: " << total_track_length << " units" << std::endl;

    // ============================================================================
    // ГЕНЕРАТОР СЛУЧАЙНЫХ ЧИСЕЛ (уникальный сид для каждой машины!)
    // ============================================================================
    std::mt19937 gen(vehicle_id * 12345 + static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<double> speed_dist(20.0, 100.0); // км/ч
    std::uniform_real_distribution<float> segment_duration_dist(3.0f, 8.0f); // секунд до смены скорости

    double currentSpeedKph = speed_dist(gen);
    float timeUntilSpeedChange = segment_duration_dist(gen);
    float timeSinceLastSpeedChange = 0.0f;
    
    // Текущая позиция на треке (в единицах дистанции, 0.0 до total_track_length)
    float currentDistance = 0.0f;

    const float update_interval_ms = SimulationConstants::UPDATE_INTERVAL_MS;
    const float deltaTime = update_interval_ms / 1000.0f; // секунды

    std::cout << "[SIM] Vehicle #" << vehicle_id << " initial speed: " << currentSpeedKph << " km/h" << std::endl;

    // Бесконечный цикл - машина всегда ездит
    while (true)
    {
        // Проверяем существование машины
        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            if (g_vehicles.find(vehicle_id) == g_vehicles.end())
            {
                std::cout << "[SIM] Vehicle #" << vehicle_id << " removed, stopping simulation" << std::endl;
                return;
            }
        }

        // ====================================================================
        // СМЕНА СКОРОСТИ НА ОСНОВЕ ВРЕМЕНИ (а не количества точек)
        // ====================================================================
        timeSinceLastSpeedChange += deltaTime;
        if (timeSinceLastSpeedChange >= timeUntilSpeedChange)
        {
            currentSpeedKph = speed_dist(gen);
            timeUntilSpeedChange = segment_duration_dist(gen);
            timeSinceLastSpeedChange = 0.0f;
        }

        // ====================================================================
        // ВЫЧИСЛЯЕМ РАССТОЯНИЕ НА ОСНОВЕ СКОРОСТИ И ВРЕМЕНИ
        // Формула: distance = speed * time
        // ====================================================================
        double speedMetersPerSecond = (currentSpeedKph * 1000.0) / 3600.0; // км/ч -> м/с
        
        // Денормализация: 1.0 normalized = MAP_SIZE метров (обычно 100м)
        // Поэтому скорость в normalized единицах = speedMetersPerSecond / MAP_SIZE
        double speedNormalizedPerSecond = speedMetersPerSecond / MapConstants::MAP_SIZE;
        
        // Дистанция за этот кадр
        float distanceThisFrame = static_cast<float>(speedNormalizedPerSecond * deltaTime);
        
        // Двигаем машину вперед
        currentDistance += distanceThisFrame;
        
        // Циклическое движение - при достижении конца возвращаемся к началу
        if (currentDistance >= total_track_length)
        {
            currentDistance = std::fmod(currentDistance, total_track_length);
        }

        // ====================================================================
        // НАХОДИМ ПОЗИЦИЮ НА ТРЕКЕ (интерполяция между точками)
        // ====================================================================
        size_t segment_index = 0;
        for (size_t i = 1; i < cumulative_distances.size(); i++)
        {
            if (cumulative_distances[i] >= currentDistance)
            {
                segment_index = i - 1;
                break;
            }
        }
        
        // Обработка краевого случая
        if (segment_index >= smooth_path.size() - 1)
        {
            segment_index = smooth_path.size() - 2;
        }
        
        // Вычисляем локальную интерполяцию внутри сегмента
        float segment_start_dist = cumulative_distances[segment_index];
        float segment_end_dist = cumulative_distances[segment_index + 1];
        float segment_length = segment_end_dist - segment_start_dist;
        
        float local_fraction = 0.0f;
        if (segment_length > 1e-6f)
        {
            local_fraction = (currentDistance - segment_start_dist) / segment_length;
            local_fraction = glm::clamp(local_fraction, 0.0f, 1.0f);
        }
        
        // Интерполяция позиции
        glm::vec2 current_pos = glm::mix(
            smooth_path[segment_index].position,
            smooth_path[segment_index + 1].position,
            local_fraction
        );

        // ====================================================================
        // КОНВЕРТИРУЕМ В GPS КООРДИНАТЫ (через UTM для точности)
        // ====================================================================
        // Normalized координаты -> метры (UTM)
        double meters_easting = g_map_origin.m_origin_meters_easting + (current_pos.x * MapConstants::MAP_SIZE);
        double meters_northing = g_map_origin.m_origin_meters_northing + (current_pos.y * MapConstants::MAP_SIZE);
        
        // Метры (UTM) -> GPS (через GeographicLib)
        double sim_lat, sim_lon;
        try {
            using namespace GeographicLib;
            UTMUPS::Reverse(g_map_origin.m_origin_zone_int, true,
                           meters_easting, meters_northing,
                           sim_lat, sim_lon);
        }
        catch (const std::exception& e) {
            std::cerr << "[SIM] GeographicLib Error: " << e.what() << std::endl;
            continue; // Skip this frame
        }

        // ====================================================================
        // СОЗДАЕМ ПАКЕТ ТЕЛЕМЕТРИИ
        // ====================================================================
        TelemetryPacket packet;
        packet.ID = vehicle_id;
        packet.lat = static_cast<int32_t>(sim_lat * 1e7);
        packet.lon = static_cast<int32_t>(sim_lon * 1e7);
        packet.speed = static_cast<uint16_t>(currentSpeedKph * 100.0);
        packet.acceleration = static_cast<int16_t>(0);
        packet.gForceX = static_cast<int16_t>(0);
        packet.gForceY = static_cast<int16_t>(0);
        packet.fixtype = 4;
        packet.time = 0;

        // ====================================================================
        // ОБНОВЛЯЕМ МАШИНУ (thread-safe)
        // ====================================================================
        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            auto it = g_vehicles.find(vehicle_id);
            
            if (it != g_vehicles.end())
            {
                Vehicle& vehicle = it->second;
                
                // Сохраняем предыдущую позицию для RaceManager
                vehicle.m_prev_x = vehicle.m_normalized_x;
                vehicle.m_prev_y = vehicle.m_normalized_y;
                
                // ✅ Сохраняем точный прогресс вдоль трека (normalized 0.0-1.0)
                vehicle.m_track_progress = static_cast<double>(currentDistance / total_track_length);
                
                // Обновляем из пакета
                vehicle.m_lat_dd = packet.lat / 1e7;
                vehicle.m_lon_dd = packet.lon / 1e7;
                vehicle.m_speed_kph = packet.speed / 100.0;
                
                coordinatesToMeters(vehicle.m_lat_dd, vehicle.m_lon_dd, vehicle.m_meters_easting, vehicle.m_meters_northing);
                getCoordinateDifferenceFromOrigin(vehicle.m_meters_easting, vehicle.m_meters_northing, 
                                                 vehicle.m_normalized_x, vehicle.m_normalized_y);
                
                vehicle.m_last_update_time = std::chrono::steady_clock::now();
            }
            else
            {
                // Создаем новую машину если она была удалена
                g_vehicles[vehicle_id] = Vehicle(packet);
            }
        }

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

    std::cout << "Starting vehicle simulation:" << std::endl;
    std::cout << "  Vehicle ID: " << vehicle_id << std::endl;
    std::cout << "  Duration: " << SimulationConstants::DEFAULT_DURATION_SECONDS << "s" << std::endl;
    std::cout << "  Path points: " << smooth_track_points.size() << std::endl;
    std::cout << "  Total distance: " << total_distance << " units" << std::endl;

    // Launch simulation in separate thread
    std::thread simulation_thread(simulationThreadWorker, vehicle_id, smooth_track_points);
    simulation_thread.detach();
}