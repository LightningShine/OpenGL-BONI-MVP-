#pragma once
#include "../input/Input.h"
#include "../network/Server.h"
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// Vehicle rendering constants
namespace VehicleConstants {
    static constexpr float VEHICLE_BODY_RADIUS = 0.05f;      // 5 meters in world (MAP_SIZE=100)
    static constexpr float VEHICLE_OUTLINE_RADIUS = 0.055f;  // White outline
    static constexpr int VEHICLE_CIRCLE_SEGMENTS = 20;
    static constexpr int VEHICLE_TIMEOUT_MS = 30000;         // 30 seconds before removal
}

class Vehicle
{
public:
    Vehicle(); // Creates vehicle at track origin
    Vehicle(const TelemetryPacket& packet); // Creates vehicle from ESP32 packet
    
    // GPS coordinates (decimal degrees)
    double m_lat_dd;
    double m_lon_dd;
    
    // UTM coordinates (meters)
    double m_meters_easting = 0;
    double m_meters_northing = 0;
    
    // Normalized OpenGL coordinates
    double m_normalized_x = 0;
    double m_normalized_y = 0;
    
    // Telemetry data
    double m_speed_kph;
    double m_acceleration;
    double m_g_force_x;
    double m_g_force_y;
    int16_t m_fix_type;
    int32_t m_id;
    
    std::string m_name = "Unknown";
    std::chrono::steady_clock::time_point m_last_update_time = std::chrono::steady_clock::now();
    glm::vec3 m_cached_color; // Color computed once at creation

    glm::vec3 getColor() const;
};

// Global vehicle system state
extern std::map<int32_t, Vehicle> g_vehicles; 
extern std::mutex g_vehicles_mutex;   
extern std::atomic<bool> g_is_vehicles_active;

// Vehicle ID generation
int32_t generateVehicleID();

// Vehicle system lifecycle
void vehicleLoop(); // Background thread for vehicle cleanup
void vehicleClose(); // Stop vehicle loop

// Rendering functions
std::vector<glm::vec2> generateCircle(float radius, int segments = 16);

void renderVehicle(GLuint shader_program, GLuint vao, GLuint vbo,
    const Vehicle& vehicle, const glm::mat4& projection);

void renderAllVehicles(GLuint shader_program, GLuint vao, GLuint vbo,
    const glm::mat4& projection,
    const glm::vec2& camera_pos, float camera_zoom);
