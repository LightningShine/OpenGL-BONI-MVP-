#include "../vehicle/Vehicle.h"
#include "../input/Input.h"
#include "../Config.h"
#include <cmath>
#include <iostream>
#include <thread>

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
std::map<int32_t, Venchile> g_Vehicles;
std::mutex g_VehiclesMutex;
std::atomic<bool> g_VehiclesActive = false;

// ✅ Генератор уникальных ID
int32_t GenerateVehicleID()
{
    static std::atomic<int32_t> nextID(1);
    return nextID++;
}

Venchile::Venchile()
{
    // ✅ Проверяем что карта загружена
    if (!m_MapLoaded)
    {
        std::cerr << "Error: Cannot create vehicle - map not loaded!" << std::endl;
        return;
    }
    
    // ✅ Используем координаты origin трека (первая точка)
    v_latDD = mapOrigin.originDD_lat;
    v_lonDD = mapOrigin.originDD_lon;
    v_speedKPH = 0.0;
    v_acceleration = 0.0;
    v_gForceX = 0.0;
    v_gForceY = 0.0;
    v_fixtype = 1;
    v_ID = GenerateVehicleID(); // ✅ Автоматическая генерация ID
    
    DDToMetr(v_latDD, v_lonDD, v_metr_easting, v_metr_north);
    CordinateDifirenceFromOrigin(v_metr_easting, v_metr_north, v_norX, v_norY);
    
    lastUpdateTime = std::chrono::steady_clock::now();
    
    // ✅ Вычисляем цвет ОДИН раз при создании
    cachedColor = GetColor();
    
    std::cout << "Vehicle #" << v_ID << " created at origin: (" << v_latDD << ", " << v_lonDD << ")" << std::endl;
}

Venchile::Venchile(const TelemetryPacket& packet)
{
    v_latDD = packet.lat / 1e7;
    v_lonDD = packet.lon / 1e7;
    v_speedKPH = packet.speed / 100.0;
    v_acceleration = packet.acceleration / 100.0;
    v_gForceX = packet.gForceX / 100.0;
    v_gForceY = packet.gForceY / 100.0;
    v_fixtype = packet.fixtype;
    v_ID = packet.ID;
    DDToMetr(v_latDD, v_lonDD, v_metr_easting, v_metr_north);
    CordinateDifirenceFromOrigin(v_metr_easting, v_metr_north, v_norX, v_norY);

    lastUpdateTime = std::chrono::steady_clock::now();
    
    // ✅ Вычисляем цвет ОДИН раз при создании
    cachedColor = GetColor();
}

glm::vec3 Venchile::GetColor() const
{
    uint32_t hash = static_cast<uint32_t>(v_ID);
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;

    float r = 0.3f + (((hash & 0xFF0000) >> 16) / 255.0f) * 0.7f;
    float g = 0.3f + (((hash & 0x00FF00) >> 8) / 255.0f) * 0.7f;
    float b = 0.3f + ((hash & 0x0000FF) / 255.0f) * 0.7f;

    return glm::vec3(r, g, b);
}

void VehicleLoop()
{
    g_VehiclesActive = true; // ✅ Используем ГЛОБАЛЬНУЮ переменную
    std::cout << "VehicleLoop started" << std::endl;
    
    while (g_VehiclesActive) 
    {
		auto now = std::chrono::steady_clock::now();

        {
        std::lock_guard<std::mutex> lock(g_VehiclesMutex);
        if (!g_Vehicles.empty())
        {
                for (auto it = g_Vehicles.begin(); it != g_Vehicles.end();)
                {
					auto timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.lastUpdateTime).count();
                    if (timeSinceLastUpdate >= VehicleConstants::VEHICLE_TIMEOUT_MS)
                    {
                        std::cout << "Vehicle ID " << it->second.v_ID << " removed due to timeout." << std::endl;
                        it = g_Vehicles.erase(it); // ✅ erase возвращает следующий итератор
                    }
                    else
                    {
                        ++it; // ✅ Инкремент ТОЛЬКО если НЕ удалили
                    }
                }           
        }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    
    std::cout << "VehicleLoop stopped" << std::endl;
}

std::vector<glm::vec2> GenerateCircle(float radius, int segments)
{
    std::vector<glm::vec2> vertices;
    vertices.reserve(segments + 2);
    vertices.push_back(glm::vec2(0.0f, 0.0f));

    for (int i = 0; i <= segments; i++) {
        float angle = 2.0f * 3.14159265359f * float(i) / float(segments);
        vertices.push_back(glm::vec2(radius * cos(angle), radius * sin(angle)));
    }
    return vertices;
}

void RenderVehicle(GLuint shaderProgram, GLuint VAO, GLuint VBO,
    const Venchile& vehicle, const glm::mat4& projection)
{
    static std::vector<glm::vec2> circleOutline = GenerateCircle(
        VehicleConstants::VEHICLE_OUTLINE_RADIUS, 
        VehicleConstants::VEHICLE_CIRCLE_SEGMENTS
    );
    static std::vector<glm::vec2> circleBody = GenerateCircle(
        VehicleConstants::VEHICLE_BODY_RADIUS, 
        VehicleConstants::VEHICLE_CIRCLE_SEGMENTS
    );
    
    // ✅ Кешируем uniform location (вычисляется только один раз)
    static GLint colorLoc = glGetUniformLocation(shaderProgram, "uColor");

    // === РИСУЕМ БЕЛУЮ ОБВОДКУ ===
    std::vector<glm::vec2> outlineVertices;
    outlineVertices.reserve(circleOutline.size());
    for (const auto& vertex : circleOutline) {
        outlineVertices.push_back(glm::vec2(
            vertex.x + static_cast<float>(vehicle.v_norX),
            vertex.y + static_cast<float>(vehicle.v_norY)
        ));
    }

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, outlineVertices.size() * sizeof(glm::vec2),
        outlineVertices.data(), GL_DYNAMIC_DRAW);

    // Белый цвет обводки
    glUniform3f(colorLoc, 1.0f, 1.0f, 1.0f);

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(outlineVertices.size()));

    // === РИСУЕМ ЦВЕТНОЕ ТЕЛО МАШИНЫ ===
    std::vector<glm::vec2> bodyVertices;
    bodyVertices.reserve(circleBody.size());
    for (const auto& vertex : circleBody) {
        bodyVertices.push_back(glm::vec2(
            vertex.x + static_cast<float>(vehicle.v_norX),
            vertex.y + static_cast<float>(vehicle.v_norY)
        ));
    }

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, bodyVertices.size() * sizeof(glm::vec2),
        bodyVertices.data(), GL_DYNAMIC_DRAW);

    // ✅ Используем кешированный цвет машины
    glUniform3f(colorLoc, vehicle.cachedColor.r, vehicle.cachedColor.g, vehicle.cachedColor.b);

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(bodyVertices.size()));
}

void RenderAllVehicles(GLuint shaderProgram, GLuint VAO, GLuint VBO,
    const glm::mat4& projection, const glm::vec2& cameraPos, float cameraZoom)
{
    // ✅ Проверяем что карта загружена
    if (!m_MapLoaded) {
        return; // Не рисуем машины если нет трека
    }
    
    // ✅ Не нужно вызывать glUseProgram и устанавливать projection
    // Это уже сделано в main.cpp перед вызовом RenderAllVehicles

    // Вычисляем границы видимости
    float visibleWidth = 1.0f / cameraZoom * 2.0f;
    float visibleHeight = 1.0f / cameraZoom * 2.0f;
    float minX = cameraPos.x - visibleWidth;
    float maxX = cameraPos.x + visibleWidth;
    float minY = cameraPos.y - visibleHeight;
    float maxY = cameraPos.y + visibleHeight;

    // ✅ БЫСТРОЕ копирование под мьютексом (~0.1ms)
    std::vector<Venchile> vehiclesToRender;
    {
        std::lock_guard<std::mutex> lock(g_VehiclesMutex);
        vehiclesToRender.reserve(g_Vehicles.size());
        
        for (const auto& [id, vehicle] : g_Vehicles) {
            float vX = static_cast<float>(vehicle.v_norX);
            float vY = static_cast<float>(vehicle.v_norY);

            // Копируем только видимые машины
            if (vX >= minX && vX <= maxX && vY >= minY && vY <= maxY) {
                vehiclesToRender.push_back(vehicle);
            }
        }
    } // ✅ Мьютекс освобожден, блокировка длилась ~0.1ms

    // ✅ Рендеринг БЕЗ блокировки (может занять 10-20ms)
    for (const auto& vehicle : vehiclesToRender) {
        RenderVehicle(shaderProgram, VAO, VBO, vehicle, projection);
    }
}

void VehicleClose()
{
	g_VehiclesActive = false;
}