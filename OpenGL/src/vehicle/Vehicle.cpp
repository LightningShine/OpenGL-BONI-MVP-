#include "../vehicle/Vehicle.h"
#include "../vehicle/VehicleInterpolator.h"
#include "../input/Input.h"
#include "../Config.h"
#include "../rendering/Interpolation.h"
#include "../rendering/VehicleNameRenderer.h"
#include "../../UI.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <thread>
#include <GeographicLib/UTMUPS.hpp>

extern UI* g_ui;

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
std::map<int32_t, Vehicle> g_vehicles;
std::mutex g_vehicles_mutex;
std::atomic<bool> g_is_vehicles_active = false;

// ✅ Система выбора машины для отслеживания
int g_focused_vehicle_id = -1;  // -1 = лидер (дефолт)

bool g_show_vehicle_names = true; // show TLA names above vehicles

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
        bool northp = (g_map_origin.m_origin_zone_char >= 'N');  // ✅ Use correct hemisphere
        UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp, 
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
    // m_apply_track_render_offset stays true (Vehicle.h default) so
    // getTrackRenderOffset() keeps vehicles aligned with the centred track.

    // Конвертируем обратно в GPS через origin UTM
    m_meters_easting = g_map_origin.m_origin_meters_easting + (m_normalized_x * MapConstants::MAP_SIZE);
    m_meters_northing = g_map_origin.m_origin_meters_northing + (m_normalized_y * MapConstants::MAP_SIZE);

    // Конвертируем UTM в GPS
    try {
        using namespace GeographicLib;
        bool northp = (g_map_origin.m_origin_zone_char >= 'N');  // ✅ Use correct hemisphere
        UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp, 
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

// ✅ Конструктор с явным ID (для симуляции)
Vehicle::Vehicle(int32_t id, double normalized_x, double normalized_y)
{
    // ✅ Проверяем что карта загружена
    if (!g_is_map_loaded)
    {
        std::cerr << "Error: Cannot create vehicle - map not loaded!" << std::endl;
        return;
    }

    // ✅ Use provided ID instead of generating
    m_id = id;

    // Устанавливаем позицию из параметров
    m_normalized_x = normalized_x;
    m_normalized_y = normalized_y;
    // m_apply_track_render_offset stays true (Vehicle.h default) so
    // getTrackRenderOffset() keeps vehicles aligned with the centred track.

    // Конвертируем обратно в GPS через origin UTM
    m_meters_easting = g_map_origin.m_origin_meters_easting + (m_normalized_x * MapConstants::MAP_SIZE);
    m_meters_northing = g_map_origin.m_origin_meters_northing + (m_normalized_y * MapConstants::MAP_SIZE);

    // Конвертируем UTM в GPS
    try {
        using namespace GeographicLib;
        int zone = g_map_origin.m_origin_zone_int;
        bool northp = (g_map_origin.m_origin_zone_char >= 'N');  // ✅ Use correct hemisphere
        UTMUPS::Reverse(zone, northp, m_meters_easting, m_meters_northing,
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

    // ✅ Initialize prev position to current (will be updated by first telemetry packet)
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

    // ⚠️ CRITICAL DEBUG: Print stack trace to find who creates this
    std::cout << "[VEHICLE CONSTRUCTOR] Creating vehicle #" << m_id 
              << " from TelemetryPacket (this should only happen for NEW vehicles!)" << std::endl;
    std::cout.flush();

    // Validate GPS coordinates
    if (std::abs(m_lat_dd) < 0.0001 || std::abs(m_lon_dd) < 0.0001) {
        std::cerr << "[VEHICLE ERROR] Invalid GPS coordinates for vehicle #" << m_id 
                  << ": lat=" << m_lat_dd << ", lon=" << m_lon_dd << std::endl;
    }

    // Convert GPS to meters
    coordinatesToMeters(m_lat_dd, m_lon_dd, m_meters_easting, m_meters_northing);

    // Check if conversion failed
    if (std::abs(m_meters_easting) < 1.0 && std::abs(m_meters_northing) < 1.0) {
        std::cerr << "[VEHICLE ERROR] coordinatesToMeters returned near-zero: "
                  << "easting=" << m_meters_easting << ", northing=" << m_meters_northing << std::endl;
    }

    // Convert meters to normalized coordinates
    getCoordinateDifferenceFromOrigin(m_meters_easting, m_meters_northing, m_normalized_x, m_normalized_y);

    std::cout << "[VEHICLE] Created vehicle #" << m_id << " at (" 
              << m_normalized_x << ", " << m_normalized_y << "), GPS: (" 
              << m_lat_dd << ", " << m_lon_dd << ")"
              << " | UTM: (" << m_meters_easting << ", " << m_meters_northing << ")" << std::endl;
    std::cout.flush();

    m_prev_x = m_normalized_x;
    m_prev_y = m_normalized_y;
    m_heading = 0.0; // Initialize heading

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
                const int timeoutMs = it->second.m_has_authoritative_state
                    ? VehicleConstants::AUTHORITATIVE_VEHICLE_TIMEOUT_MS
                    : VehicleConstants::VEHICLE_TIMEOUT_MS;

                if (timeSinceLastUpdate >= timeoutMs)
                {
                    std::cout << "[TIMEOUT] Vehicle ID #" << it->second.m_id 
                              << " removed due to timeout (" << timeSinceLastUpdate << "ms > " 
                              << timeoutMs << "ms)" << std::endl;
                    std::cout.flush();
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

    // ========================================================================
    // ✅ CALCULATE ROTATION ANGLE WITH PERSISTENCE
    // Caches last valid angle to prevent flickering when GPS jitter causes
    // movement < MIN_MOVEMENT threshold. Uses exponential smoothing for gradual rotation.
    // ========================================================================
    float rotationAngle = vehicle.m_last_rotation_angle;  // ✅ Start with cached angle

    if (vehicle.m_is_leader)
    {
        // Use authoritative heading if available. This is more stable than deriving
        // rotation from frame-to-frame position differences.
        float newAngle = static_cast<float>(vehicle.m_heading) - glm::half_pi<float>();

        // ✅ SMOOTH INTERPOLATION (exponential smoothing)
        const float SMOOTHING_FACTOR = 0.3f;  // 0.0 = no change, 1.0 = instant (0.3 = good balance)

        // Handle angle wrapping (-PI to PI)
        float angleDiff = newAngle - vehicle.m_last_rotation_angle;

        if (angleDiff > glm::pi<float>())
            angleDiff -= 2.0f * glm::pi<float>();
        else if (angleDiff < -glm::pi<float>())
            angleDiff += 2.0f * glm::pi<float>();

        rotationAngle = vehicle.m_last_rotation_angle + angleDiff * SMOOTHING_FACTOR;

        vehicle.m_last_rotation_angle = rotationAngle;
    }

    // ✅ ОПТИМИЗАЦИЯ: Создаем матрицу вращения один раз
    glm::mat2 rotationMatrix(1.0f);
    if (vehicle.m_is_leader)
    {
        float cosAngle = std::cos(rotationAngle);
        float sinAngle = std::sin(rotationAngle);
        rotationMatrix = glm::mat2(
            cosAngle, sinAngle,
            -sinAngle, cosAngle
        );
    }

  const glm::vec2 renderOffset = vehicle.m_apply_track_render_offset ? getTrackRenderOffset() : glm::vec2(0.0f, 0.0f);
    const float baseX = static_cast<float>(vehicle.m_normalized_x) + renderOffset.x;
    const float baseY = static_cast<float>(vehicle.m_normalized_y) + renderOffset.y;

    // === РИСУЕМ БЕЛУЮ ОБВОДКУ ===
    std::vector<glm::vec2> outlineVertices;
    outlineVertices.reserve(outlineShape.size());
    for (const auto& vertex : outlineShape) {
        // ✅ Применяем матрицу поворота (экономит вычисления cos/sin)
        glm::vec2 transformedVertex = (vehicle.m_is_leader) 
            ? rotationMatrix * vertex 
            : vertex;
        
        outlineVertices.push_back(glm::vec2(
           transformedVertex.x + baseX,
            transformedVertex.y + baseY
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
        glm::vec2 transformedVertex = (vehicle.m_is_leader) 
            ? rotationMatrix * vertex 
            : vertex;
        
        bodyVertices.push_back(glm::vec2(
           transformedVertex.x + baseX,
            transformedVertex.y + baseY
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

    // Get current render time for interpolation
    double renderTime = VehicleInterpolator::GetTime();

    // Вычисляем границы видимости
    float visibleWidth = 1.0f / camera_zoom * 2.0f;
    float visibleHeight = 1.0f / camera_zoom * 2.0f;
    float minX = camera_pos.x - visibleWidth;
    float maxX = camera_pos.x + visibleWidth;
    float minY = camera_pos.y - visibleHeight;
    float maxY = camera_pos.y + visibleHeight;

    // ✅ Собираем копии машин с интерполированными позициями
    struct RenderData {
        Vehicle vehicle;  // Copy for thread-safe rendering
        double interp_x, interp_y, interp_heading, interp_speed;
        bool use_interpolation;
    };

    std::vector<RenderData> vehiclesToRender;
    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
        vehiclesToRender.reserve(g_vehicles.size());

        for (const auto& [id, vehicle] : g_vehicles) {
            RenderData data{ vehicle, 0.0, 0.0, 0.0, 0.0, false };

            // Try to get interpolated position
            if (VehicleInterpolator::Get().GetInterpolatedState(
                id, renderTime,
                data.interp_x, data.interp_y, 
                data.interp_heading, data.interp_speed))
            {
                data.use_interpolation = true;

                // Check visibility with interpolated position
                if (data.interp_x >= minX && data.interp_x <= maxX &&
                    data.interp_y >= minY && data.interp_y <= maxY)
                {
                    // ✅ Use interpolated position AND heading for rendering
                    data.vehicle.m_normalized_x = data.interp_x;
                    data.vehicle.m_normalized_y = data.interp_y;
                    data.vehicle.m_speed_kph = data.interp_speed;
                    data.vehicle.m_heading = data.interp_heading;  // ✅ Fix: update heading too!

                    // Update prev position for direction calculation
                    data.vehicle.m_prev_x = vehicle.m_prev_x;
                    data.vehicle.m_prev_y = vehicle.m_prev_y;

                    vehiclesToRender.push_back(data);
                }
            }
            else
            {
                // ✅ Fallback to direct position (no interpolation data yet or buffer not ready)
                // This happens in first few frames or if packets are lost
                float vX = static_cast<float>(vehicle.m_normalized_x);
                float vY = static_cast<float>(vehicle.m_normalized_y);

                if (vX >= minX && vX <= maxX && vY >= minY && vY <= maxY) {
                    vehiclesToRender.push_back(data);
                }
            }
        }
    } // ✅ Мьютекс освобожден

    // ✅ Рендеринг БЕЗ блокировки (может занять 10-20ms)
    for (const RenderData& data : vehiclesToRender) {
        renderVehicle(shader_program, vao, vbo, data.vehicle, projection);
    }

    // Draw TLA names above each vehicle if enabled.
    // Apply the same track-centering offset used by the dot so label and
    // dot always land at the same screen position.
    if (g_show_vehicle_names) {
        for (const RenderData& data : vehiclesToRender) {
            if (!data.vehicle.name.empty()) {
                const glm::vec2 rOff = data.vehicle.m_apply_track_render_offset
                                         ? getTrackRenderOffset()
                                         : glm::vec2(0.0f, 0.0f);
                VehicleNameRenderer::DrawName(
                    data.vehicle.name,
                    static_cast<float>(data.vehicle.m_normalized_x) + rOff.x,
                    static_cast<float>(data.vehicle.m_normalized_y) + rOff.y,
                    projection, g_ui ? g_ui->GetTitleFont() : nullptr, 1.0f);
            }
        }
    }
}

void vehicleClose()
{
	g_is_vehicles_active = false;
}