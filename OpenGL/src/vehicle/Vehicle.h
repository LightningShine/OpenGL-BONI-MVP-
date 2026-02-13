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

// ✅ Система выбора машины для отслеживания
extern int g_focused_vehicle_id;  // -1 = лидер (дефолт), иначе ID машины

// ============================================================================
// LAP DATA STRUCTURE (integrated into Vehicle for storage)
// ============================================================================
struct LapData
{
	float lapTime;                              // Lap time in seconds
	int positionAtFinish;                       // Position when crossing line
	std::vector<glm::vec2> telemetryPoints;     // Placeholder for future telemetry
	
	LapData() : lapTime(0.0f), positionAtFinish(0) {}
	LapData(float time, int position) : lapTime(time), positionAtFinish(position) {}
};

class Vehicle
{
public:
	Vehicle();
	Vehicle(double normalized_x, double normalized_y); // ✅ Конструктор с начальной позицией
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
	bool m_is_leader = false;  // ✅ Флаг лидера гонки (треугольник вместо круга)
	
	// ========================================================================
	// LAP TIMING DATA (managed by RaceManager, stored in Vehicle)
	// ========================================================================
	std::map<int, LapData> m_laps;              // Map: lap number -> LapData
	float m_current_lap_timer = 0.0f;           // Current lap timer (seconds)
	int m_current_lap_number = 1;               // Current lap being driven
	int m_completed_laps = 0;                   // Number of completed laps
	bool m_has_started_first_lap = false;       // True after first S/F crossing
	double m_prev_x = 0.0;                      // Previous frame position (for intersection)
	double m_prev_y = 0.0;                      // Previous frame position (for intersection)
	float m_best_lap_time = 999999.0f;          // Best lap time (seconds)
	
	// ✅ Track progress (normalized 0.0-1.0 for accurate position determination)
	double m_track_progress = 0.0;              // Accumulated distance along track (0.0 = start, 1.0 = full lap)
	
	// ✅ Smoothed rotation angle for triangle rendering (radians)
	float m_smoothed_rotation = 0.0f;           // Сглаженный угол поворота для плавного рендеринга

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
std::vector<glm::vec2> generateTriangle(float size); // ✅ Треугольник для лидера

void renderVehicle(GLuint shader_program, GLuint vao, GLuint vbo,
	const Vehicle& vehicle, const glm::mat4& projection);

void renderAllVehicles(GLuint shader_program, GLuint vao, GLuint vbo,
	const glm::mat4& projection,
	const glm::vec2& camera_pos, float camera_zoom);

void removeVehicles();

void vehicleClose();