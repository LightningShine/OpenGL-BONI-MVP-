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

extern int g_focused_vehicle_id;  // -1 = лидер (дефолт), иначе ID машины

struct LapData
{
	float lapTime;                              // Lap time in seconds
	int positionAtFinish;                       // Position when crossing line
	std::vector<glm::vec2> telemetryPoints;     // Placeholder for future telemetry
	
	LapData() : lapTime(0.0f), positionAtFinish(0) {}
	LapData(float time, int position) : lapTime(time), positionAtFinish(position) {}
};


struct LapInfo
{
	float timefromstart;
	double progress;
	float gForceX, gForceY;
	float aceleration, speed;
	int curentPosition;
};

struct CarLapSessions
{
	int lapnumber;
	int globalLapnumber;
	int bestlapID;
	std::vector<LapInfo> samples;
};




class Vehicle
{
public:
	Vehicle();
	Vehicle(double normalized_x, double normalized_y); 
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
	glm::vec3 m_cached_color; 
	bool m_is_leader = false;  
	
	// ========================================================================
	// LAP TIMING DATA (RaceManager reads/writes, Vehicle stores)
	// ========================================================================
	std::map<int, LapData> m_laps;
	float m_current_lap_timer = 0.0f;
	int m_current_lap_number = 1;
	int m_completed_laps = 0;
	bool m_has_started_first_lap = false;
	float m_best_lap_time = -1.0f;
	
	// ========================================================================
	// POSITION TRACKING (for line crossing detection)
	// ========================================================================
	double m_prev_x = 0.0;
	double m_prev_y = 0.0;
	
	// ========================================================================
	// TRACK PROGRESS (0.0 = start, 1.0 = full lap)
	// ========================================================================
	double m_track_progress = 0.0;
	double m_prev_track_progress = 0.0;
	
	// ========================================================================
	// FUTURE: Detailed telemetry history
	// ========================================================================
	std::map<float, double> lapHistory;
	std::map<float, double> bestLap;
	std::map<int, CarLapSessions> laps;
	
	// ========================================================================
	// COLOR GENERATION
	// ========================================================================
	glm::vec3 getColor() const;
};


extern std::map<int32_t, Vehicle> g_vehicles; 
extern std::mutex g_vehicles_mutex;   
extern std::atomic<bool> g_is_vehicles_active;









// === Function ===
void vehicleLoop(); // Главный цикл обновления машин

int32_t generateVehicleID();

std::vector<glm::vec2> generateCircle(float radius, int segments = 16);
std::vector<glm::vec2> generateTriangle(float size); // ✅ Треугольник для лидера

void renderVehicle(GLuint shader_program, GLuint vao, GLuint vbo,
	const Vehicle& vehicle, const glm::mat4& projection);

void renderAllVehicles(GLuint shader_program, GLuint vao, GLuint vbo,
	const glm::mat4& projection,
	const glm::vec2& camera_pos, float camera_zoom);

void removeVehicles();

void vehicleClose();