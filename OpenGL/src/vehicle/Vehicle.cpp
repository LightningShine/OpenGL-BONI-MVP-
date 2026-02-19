#include "../vehicle/Vehicle.h"
#include "../input/Input.h"
#include "../Config.h"
#include <cmath>
#include <iostream>
#include <thread>
#include <GeographicLib/UTMUPS.hpp>

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
std::map<int32_t, Vehicle> g_vehicles;
std::mutex g_vehicles_mutex;
std::atomic<bool> g_is_vehicles_active = false;

// ✅ Система выбора машины для отслеживания
int g_focused_vehicle_id = -1;  // -1 = лидер (дефолт)

// ✅ Генератор уникальных ID
int32_t generateVehicleID()
{
    static std::atomic<int32_t> nextID(1);
    return nextID++;
}

Vehicle::Vehicle()
{
    // ✅ Проверяем что карта загружена
    if (!g_is_map_loaded)
    {
        std::cerr << "Error: Cannot create vehicle - map not loaded!" << std::endl;
        return;
    }
    

    m_normalized_x = 0.0;
    m_normalized_y = 0.0;
    
    // Конвертируем обратно в GPS через origin UTM
    m_meters_easting = g_map_origin.m_origin_meters_easting + (m_normalized_x * MapConstants::MAP_SIZE);
    m_meters_northing = g_map_origin.m_origin_meters_northing + (m_normalized_y * MapConstants::MAP_SIZE);
    
    // Конвертируем UTM в GPS
    try {
        using namespace GeographicLib;
        UTMUPS::Reverse(g_map_origin.m_origin_zone_int, true, 
                       m_meters_easting, m_meters_northing, 
                       m_lat_dd, m_lon_dd);
    }
    catch (const std::exception& e) {
        std::cerr << "GeographicLib Error: " << e.what() << std::endl;
        m_lat_dd = 0;
        m_lon_dd = 0;
    }
    
    m_speed_kph = 0.0;
    m_acceleration = 0.0;
    m_g_force_x = 0.0;
    m_g_force_y = 0.0;
    m_fix_type = 1;
    m_id = generateVehicleID();
    
    m_last_update_time = std::chrono::steady_clock::now();
    
    // ✅ Вычисляем цвет ОДИН раз при создании
    m_cached_color = getColor();
    
    std::cout << "Vehicle #" << m_id << " created at center (0, 0), GPS: (" << m_lat_dd << ", " << m_lon_dd << ")" << std::endl;
}

// ✅ Конструктор с начальной позицией на треке
Vehicle::Vehicle(double normalized_x, double normalized_y)
{
    // ✅ Проверяем что карта загружена
    if (!g_is_map_loaded)
    {
        std::cerr << "Error: Cannot create vehicle - map not loaded!" << std::endl;
        return;
    }
    
    // Устанавливаем позицию из параметров
    m_normalized_x = normalized_x;
    m_normalized_y = normalized_y;
    
    // Конвертируем обратно в GPS через origin UTM
    m_meters_easting = g_map_origin.m_origin_meters_easting + (m_normalized_x * MapConstants::MAP_SIZE);
    m_meters_northing = g_map_origin.m_origin_meters_northing + (m_normalized_y * MapConstants::MAP_SIZE);
    
    // Конвертируем UTM в GPS
    try {
        using namespace GeographicLib;
        UTMUPS::Reverse(g_map_origin.m_origin_zone_int, true, 
                       m_meters_easting, m_meters_northing, 
                       m_lat_dd, m_lon_dd);
    }
    catch (const std::exception& e) {
        std::cerr << "GeographicLib Error: " << e.what() << std::endl;
        m_lat_dd = 0;
        m_lon_dd = 0;
    }
    
    m_speed_kph = 0.0;
    m_acceleration = 0.0;
    m_g_force_x = 0.0;
    m_g_force_y = 0.0;
    m_fix_type = 1;
    m_id = generateVehicleID();
    
    // ✅ Устанавливаем prev позицию такой же (чтобы не было ложного пересечения)
    m_prev_x = m_normalized_x;
    m_prev_y = m_normalized_y;
    
    m_last_update_time = std::chrono::steady_clock::now();
    
    // ✅ Вычисляем цвет ОДИН раз при создании
    m_cached_color = getColor();
    
    std::cout << "Vehicle #" << m_id << " created at START line (" << m_normalized_x << ", " << m_normalized_y << "), GPS: (" << m_lat_dd << ", " << m_lon_dd << ")" << std::endl;
}

Vehicle::Vehicle(const TelemetryPacket& packet)
{
    m_lat_dd = packet.lat / 1e7;
    m_lon_dd = packet.lon / 1e7;
    m_speed_kph = packet.speed / 100.0;
    m_acceleration = packet.acceleration / 100.0;
    m_g_force_x = packet.gForceX / 100.0;
    m_g_force_y = packet.gForceY / 100.0;
    m_fix_type = packet.fixtype;
    m_id = packet.ID;
    
    std::cout << "[VEHICLE] Creating vehicle #" << m_id << " from telemetry packet" << std::endl;
    
    coordinatesToMeters(m_lat_dd, m_lon_dd, m_meters_easting, m_meters_northing);
    getCoordinateDifferenceFromOrigin(m_meters_easting, m_meters_northing, m_normalized_x, m_normalized_y);

    m_prev_x = m_normalized_x;
    m_prev_y = m_normalized_y;
    
    m_last_update_time = std::chrono::steady_clock::now();
    m_cached_color = getColor();
}

glm::vec3 Vehicle::getColor() const
{
    uint32_t hash = static_cast<uint32_t>(m_id);
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;

    float r = 0.3f + (((hash & 0xFF0000) >> 16) / 255.0f) * 0.7f;
    float g = 0.3f + (((hash & 0x00FF00) >> 8) / 255.0f) * 0.7f;
    float b = 0.3f + ((hash & 0x0000FF) / 255.0f) * 0.7f;

    return glm::vec3(r, g, b);
}

void vehicleLoop()
{
    g_is_vehicles_active = true; // ✅ Используем ГЛОБАЛЬНУЮ переменную
    std::cout << "vehicleLoop started" << std::endl;
    
	while (g_is_vehicles_active) // Need to chabge ( g_is_vehicles_active is all time true, but we need check if car movving or send packed if not then remove venchile 
                                 // and replace checking to functiont ) 
    {
        removeVehicles();

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    
    std::cout << "vehicleLoop stopped" << std::endl;
}

void removeVehicles()
{
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
        if (!g_vehicles.empty())
        {
            for (auto it = g_vehicles.begin(); it != g_vehicles.end();)
            {
                auto timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.m_last_update_time).count();
                if (timeSinceLastUpdate >= VehicleConstants::VEHICLE_TIMEOUT_MS)
                {
                    std::cout << "Vehicle ID " << it->second.m_id << " removed due to timeout." << std::endl;
                    it = g_vehicles.erase(it); // ✅ erase возвращает следующий итератор
                }
                else
                {
                    ++it; // ✅ Инкремент ТОЛЬКО если НЕ удалили
                }
            }
        }
        
    }
}


std::vector<glm::vec2> generateCircle(float radius, int segments)
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

// ✅ Генерация треугольника для лидера (вершина вверх)
// Размер треугольника соответствует диаметру круга:
// - Основание = 2 * size (равно диаметру круга)
// - Высота = 1.2 * size (пропорциональный треугольник)
std::vector<glm::vec2> generateTriangle(float size)
{
    std::vector<glm::vec2> vertices;
    vertices.reserve(4);
    vertices.push_back(glm::vec2(0.0f, 0.0f)); // Центр
    
    // Треугольник с вершиной направленной вперёд
    vertices.push_back(glm::vec2(0.0f, size * 1.2f));     // Верх (ОСТРЫЙ КОНЕЦ)
    vertices.push_back(glm::vec2(-size, -size * 0.8f));   // Левый низ
    vertices.push_back(glm::vec2(size, -size * 0.8f));    // Правый низ (основание = 2*size)
    vertices.push_back(glm::vec2(0.0f, size * 1.2f));     // Замыкаем
    
    return vertices;
}

void renderVehicle(GLuint shader_program, GLuint vao, GLuint vbo,
    const Vehicle& vehicle, const glm::mat4& projection)
{
    // ✅ Статические геометрии (генерируются один раз для производительности)
    static std::vector<glm::vec2> circleOutline = generateCircle(
        VehicleConstants::VEHICLE_OUTLINE_RADIUS, 
        VehicleConstants::VEHICLE_CIRCLE_SEGMENTS
    );
    static std::vector<glm::vec2> circleBody = generateCircle(
        VehicleConstants::VEHICLE_BODY_RADIUS, 
        VehicleConstants::VEHICLE_CIRCLE_SEGMENTS
    );
    static std::vector<glm::vec2> triangleOutline = generateTriangle(VehicleConstants::VEHICLE_OUTLINE_RADIUS);
    static std::vector<glm::vec2> triangleBody = generateTriangle(VehicleConstants::VEHICLE_BODY_RADIUS);
    
    // ✅ Кешируем uniform location (вычисляется только один раз)
    static GLint colorLoc = glGetUniformLocation(shader_program, "uColor");
    
    // ✅ Выбираем форму: треугольник для лидера, круг для остальных
    const std::vector<glm::vec2>& outlineShape = vehicle.m_is_leader ? triangleOutline : circleOutline;
    const std::vector<glm::vec2>& bodyShape = vehicle.m_is_leader ? triangleBody : circleBody;

    // ✅ ВЫЧИСЛЯЕМ УГОЛ ПОВОРОТА для треугольника лидера (САМЫЙ ПЕРВЫЙ АЛГОРИТМ - без сглаживания)
    float rotationAngle = 0.0f;
    if (vehicle.m_is_leader)
    {
        // Вектор направления движения
        glm::vec2 direction(
            vehicle.m_normalized_x - vehicle.m_prev_x,
            vehicle.m_normalized_y - vehicle.m_prev_y
        );
        
        float length = glm::length(direction);
        if (length > 0.0001f) // Проверяем что машина движется
        {
            // ✅ ПРЯМОЕ ВЫЧИСЛЕНИЕ УГЛА (без сглаживания - самый первый алгоритм)
            // atan2 возвращает угол от оси X
            // Вычитаем PI/2 потому что треугольник по умолчанию смотрит вверх (ось Y)
            rotationAngle = std::atan2(direction.y, direction.x) - glm::half_pi<float>();
        }
    }

    // ✅ ОПТИМИЗАЦИЯ: Создаем матрицу вращения один раз
    glm::mat2 rotationMatrix(1.0f);
    if (vehicle.m_is_leader && rotationAngle != 0.0f)
    {
        float cosAngle = std::cos(rotationAngle);
        float sinAngle = std::sin(rotationAngle);
        rotationMatrix = glm::mat2(
            cosAngle, sinAngle,
            -sinAngle, cosAngle
        );
    }

    // === РИСУЕМ БЕЛУЮ ОБВОДКУ ===
    std::vector<glm::vec2> outlineVertices;
    outlineVertices.reserve(outlineShape.size());
    for (const auto& vertex : outlineShape) {
        // ✅ Применяем матрицу поворота (экономит вычисления cos/sin)
        glm::vec2 transformedVertex = (vehicle.m_is_leader && rotationAngle != 0.0f) 
            ? rotationMatrix * vertex 
            : vertex;
        
        outlineVertices.push_back(glm::vec2(
            transformedVertex.x + static_cast<float>(vehicle.m_normalized_x),
            transformedVertex.y + static_cast<float>(vehicle.m_normalized_y)
        ));
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, outlineVertices.size() * sizeof(glm::vec2),
        outlineVertices.data(), GL_DYNAMIC_DRAW);

    // ✅ Цвет обводки из конфига (для машины и лидера одинаковый)
    glUniform3f(colorLoc, 
        VehicleConstants::VEHICLE_OUTLINE_COLOR_R,
        VehicleConstants::VEHICLE_OUTLINE_COLOR_G,
        VehicleConstants::VEHICLE_OUTLINE_COLOR_B);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(outlineVertices.size()));

    // === РИСУЕМ ЦВЕТНОЕ ТЕЛО МАШИНЫ ===
    std::vector<glm::vec2> bodyVertices;
    bodyVertices.reserve(bodyShape.size());
    for (const auto& vertex : bodyShape) {
        // ✅ Применяем матрицу поворота (экономит вычисления cos/sin)
        glm::vec2 transformedVertex = (vehicle.m_is_leader && rotationAngle != 0.0f) 
            ? rotationMatrix * vertex 
            : vertex;
        
        bodyVertices.push_back(glm::vec2(
            transformedVertex.x + static_cast<float>(vehicle.m_normalized_x),
            transformedVertex.y + static_cast<float>(vehicle.m_normalized_y)
        ));
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, bodyVertices.size() * sizeof(glm::vec2),
        bodyVertices.data(), GL_DYNAMIC_DRAW);

    // ✅ Используем кешированный цвет машины
    glUniform3f(colorLoc, vehicle.m_cached_color.r, vehicle.m_cached_color.g, vehicle.m_cached_color.b);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(bodyVertices.size()));
}

void renderAllVehicles(GLuint shader_program, GLuint vao, GLuint vbo,
    const glm::mat4& projection, const glm::vec2& camera_pos, float camera_zoom)
{
    // ✅ Проверяем что карта загружена
    if (!g_is_map_loaded) {
        return; // Не рисуем машины если нет трека
    }
    
    // ✅ Не нужно вызывать glUseProgram и устанавливать projection
    // Это уже сделано в main.cpp перед вызовом renderAllVehicles

    // Вычисляем границы видимости
    float visibleWidth = 1.0f / camera_zoom * 2.0f;
    float visibleHeight = 1.0f / camera_zoom * 2.0f;
    float minX = camera_pos.x - visibleWidth;
    float maxX = camera_pos.x + visibleWidth;
    float minY = camera_pos.y - visibleHeight;
    float maxY = camera_pos.y + visibleHeight;

    // ✅ БЫСТРОЕ копирование под мьютексом (~0.1ms)
    std::vector<Vehicle> vehiclesToRender;
    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
        vehiclesToRender.reserve(g_vehicles.size());
        
        for (const auto& [id, vehicle] : g_vehicles) {
            float vX = static_cast<float>(vehicle.m_normalized_x);
            float vY = static_cast<float>(vehicle.m_normalized_y);

            // Копируем только видимые машины
            if (vX >= minX && vX <= maxX && vY >= minY && vY <= maxY) {
                vehiclesToRender.push_back(vehicle);
            }
        }
    } // ✅ Мьютекс освобожден, блокировка длилась ~0.1ms

    // ✅ Рендеринг БЕЗ блокировки (может занять 10-20ms)
    for (const auto& vehicle : vehiclesToRender) {
        renderVehicle(shader_program, vao, vbo, vehicle, projection);
    }
}

void vehicleClose()
{
	g_is_vehicles_active = false;
}