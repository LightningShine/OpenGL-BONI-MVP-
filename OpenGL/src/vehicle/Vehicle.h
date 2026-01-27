#pragma once
#include "../input/Input.h"
#include "../network/Server.h"
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Vehicle
{
public:
	Vehicle();
	Vehicle(const TelemetryPacket& packet);
	
	double m_lat_dd;
	double m_lon_dd;
	double m_meters_easting = 0;
	double m_meters_northing = 0;
	double m_normalized_x = 0;
	double m_normalized_y = 0;
	double m_speed_kph;
	double m_acceleration;
	double m_g_force_x;
	double m_g_force_y;
	int16_t m_fix_type;
	int32_t m_id;
	std::string name = "Unknown";
	std::chrono::steady_clock::time_point m_last_update_time = std::chrono::steady_clock::now();
	glm::vec3 m_cached_color; // ✅ Кешируем цвет при создании

	glm::vec3 getColor() const;	
};


extern std::map<int32_t, Vehicle> g_vehicles; 
extern std::mutex g_vehicles_mutex;   
extern std::atomic<bool> g_is_vehicles_active;

// ✅ Генератор уникальных ID для машин
int32_t generateVehicleID();

// === ФУНКЦИИ ===
void vehicleLoop(); // Главный цикл обновления машин

std::vector<glm::vec2> generateCircle(float radius, int segments = 16);

void renderVehicle(GLuint shader_program, GLuint vao, GLuint vbo,
	const Vehicle& vehicle, const glm::mat4& projection);

void renderAllVehicles(GLuint shader_program, GLuint vao, GLuint vbo,
	const glm::mat4& projection,
	const glm::vec2& camera_pos, float camera_zoom);

void removeVehicles();

void vehicleClose();