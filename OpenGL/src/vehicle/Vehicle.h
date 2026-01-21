#pragma once
#include "../input/Input.h"
#include "../network/Server.h"
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Venchile
{
public:
	Venchile(); // Конструктор по умолчанию (создает машину на origin трека)
	Venchile(const TelemetryPacket &packet); // Конструктор из пакета ESP32
	
	double v_latDD;
	double v_lonDD;
	double v_metr_easting = 0;
	double v_metr_north = 0;
	double v_norX = 0;
	double v_norY = 0;
	double v_speedKPH;
	double v_acceleration;
	double v_gForceX;
	double v_gForceY;
	int16_t v_fixtype;
	int32_t v_ID;
	std::string name = "Unknown";
	std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::now();
	glm::vec3 cachedColor; // ✅ Кешируем цвет при создании

	glm::vec3 GetColor() const;	
};


extern std::map<int32_t, Venchile> g_Vehicles; 
extern std::mutex g_VehiclesMutex;   
extern std::atomic<bool> g_VehiclesActive;

// ✅ Генератор уникальных ID для машин
int32_t GenerateVehicleID();

// === ФУНКЦИИ ===
void VehicleLoop(); // Главный цикл обновления машин

std::vector<glm::vec2> GenerateCircle(float radius, int segments = 16);

void RenderVehicle(GLuint shaderProgram, GLuint VAO, GLuint VBO,
	const Venchile& vehicle, const glm::mat4& projection);

void RenderAllVehicles(GLuint shaderProgram, GLuint VAO, GLuint VBO,
	const glm::mat4& projection,
	const glm::vec2& cameraPos, float cameraZoom);

void VehicleClose();