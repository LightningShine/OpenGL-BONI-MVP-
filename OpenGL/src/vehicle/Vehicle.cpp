#include "../vehicle/Vehicle.h"
#include "../vehicle/VehicleInterpolator.h"
#include "../input/Input.h"
#include "../Config.h"
#include "../rendering/Interpolation.h"
#include "../rendering/VehicleNameRenderer.h"
#include "../network/SimulationServer.h"
#include "../core/WorldSnapshot.h"
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
std::atomic<bool> g_race_session_active = false;
std::atomic<bool> g_pipeline_rebuilding = false;
std::atomic<bool> g_position_smoothing_enabled = true;

// Глубина захвата мьютекса машин ЭТИМ потоком. Ноль означает «не держим».
// Пересчёт повтора поднимает её на весь прогон, и вложенные захваты внутри
// приёма пакета становятся пустыми операциями.
static thread_local int t_vehicles_lock_depth = 0;

VehiclesLock::VehiclesLock()
    : owns_(t_vehicles_lock_depth == 0)
{
    if (owns_)
    {
        g_vehicles_mutex.lock();
        ++t_vehicles_lock_depth;
    }
}

VehiclesLock::~VehiclesLock()
{
    if (owns_)
    {
        --t_vehicles_lock_depth;
        g_vehicles_mutex.unlock();
    }
}

void enter_vehicles_bulk_section()
{
    g_vehicles_mutex.lock();
    ++t_vehicles_lock_depth;
}

void leave_vehicles_bulk_section()
{
    --t_vehicles_lock_depth;
    g_vehicles_mutex.unlock();
}

// ✅ Система выбора машины для отслеживания
int g_focused_vehicle_id = -1;  // -1 = лидер (дефолт)

bool g_show_vehicle_names = true; // show TLA names above vehicles

// ✅ Генератор уникальных ID
size_t visibleSampleCount(const Vehicle& vehicle, int lapNumber,
                          const std::vector<LapInfo>& samples)
{
    // Круга ещё не было: он целиком в непроигранной части записи.
    if (lapNumber > vehicle.m_current_lap_number)
        return 0;

    // Круг пройден целиком — виден весь.
    if (lapNumber < vehicle.m_current_lap_number)
        return samples.size();

    // Текущий круг виден до места, где машина стоит сейчас. Прогресс внутри
    // круга возрастает, поэтому границу ищем двоичным поиском: панели читают
    // историю каждый кадр, и перебирать её целиком нельзя.
    const auto cut = std::upper_bound(
        samples.begin(), samples.end(), vehicle.m_track_progress,
        [](double progress, const LapInfo& sample) { return progress < sample.progress; });

    return static_cast<size_t>(cut - samples.begin());
}

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

Vehicle::Vehicle(int32_t race_id, const TelemetryPacket& packet)
{
    m_lat_dd = packet.lat / 1e7;
    m_lon_dd = packet.lon / 1e7;
    m_speed_kph = packet.speed / 100.0;
    // Ускорение — тоже со знаком: торможение это отрицательное ускорение, и
    // прочтение беззнаковым превращало его в десятки миллионов м/с².
    m_acceleration = static_cast<int32_t>(packet.acceleration) / 100.0;
    // Перегрузка со ЗНАКОМ: в пакете поле беззнаковое, но торможение и левый
    // поворот — это отрицательные значения, и трекер кладёт их дополнительным
    // кодом. Для любой физически возможной перегрузки (до 327 g) прочтение как
    // int16 совпадает с прежним, а отрицательные перестают превращаться в
    // сотни g.
    m_g_force_x = static_cast<int16_t>(packet.gForceX) / 100.0;
    m_g_force_y = static_cast<int16_t>(packet.gForceY) / 100.0;
    m_fix_type = packet.fixtype;
    m_id = race_id;
    m_device_id = packet.ID;

    // ⚠️ CRITICAL DEBUG: Print stack trace to find who creates this
    std::cout << "[VEHICLE CONSTRUCTOR] Creating vehicle #" << m_id
              << " (device #" << m_device_id << ")"
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
    // Собираем race ID удалённых машин под локом, а сопутствующее состояние
    // (маппинг, интерполятор, тайм-синк) чистим ПОСЛЕ выхода из g_vehicles_mutex:
    // allocate-путь берёт s_proto_map_mutex, затем g_vehicles_mutex, поэтому
    // обратный порядок здесь привёл бы к дедлоку.
    std::vector<int32_t> removed_race_ids;
    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
        // Во время сессии участник, потерявший связь, помечается, но не удаляется:
        // всё его гоночное состояние (круги, лучшее время) живёт внутри объекта и
        // умерло бы вместе с ним, а после реконнекта машина начала бы гонку с нуля.
        const bool keep_entrants = g_race_session_active.load(std::memory_order_relaxed);

        for (auto it = g_vehicles.begin(); it != g_vehicles.end();)
        {
            const int32_t race_id = it->first;
            Vehicle& vehicle = it->second;

            const auto silence_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - vehicle.m_last_update_time).count();
            const int timeoutMs = vehicle.m_has_authoritative_state
                ? VehicleConstants::AUTHORITATIVE_VEHICLE_TIMEOUT_MS
                : VehicleConstants::VEHICLE_TIMEOUT_MS;

            if (silence_ms < timeoutMs)
            {
                vehicle.m_signal_lost = false;
                ++it;
                continue;
            }

            // Машины с авторитетным состоянием считает сервер: их круги живут не
            // здесь, сохранять объект незачем — пусть уходят быстро, как и раньше.
            if (keep_entrants && !vehicle.m_has_authoritative_state)
            {
                if (!vehicle.m_signal_lost)
                {
                    vehicle.m_signal_lost = true;
                    std::cout << "[SIGNAL] Vehicle #" << race_id
                              << " (device #" << vehicle.m_device_id
                              << ") lost signal after " << silence_ms
                              << "ms, kept: session is running" << std::endl;
                    std::cout.flush();
                }
                ++it;
                continue;
            }

            std::cout << "[TIMEOUT] Vehicle #" << race_id
                      << " (device #" << vehicle.m_device_id
                      << ") removed due to timeout (" << silence_ms << "ms > "
                      << timeoutMs << "ms)" << std::endl;
            std::cout.flush();
            removed_race_ids.push_back(race_id);
            it = g_vehicles.erase(it); // ✅ erase возвращает следующий итератор
        }
    }

    // Машины ушли окончательно — единой точкой чистим всё, что заведено на их
    // race ID (уже без g_vehicles_mutex).
    for (int32_t race_id : removed_race_ids)
        telemetryForgetVehicle(race_id);
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
    const VehicleRenderState& vehicle, const glm::mat4& projection, float camera_zoom)
{
    // Сглаживание поворота лидера должно переживать кадр, поэтому угол хранится
    // здесь, по ID машины. Раньше он лежал в Vehicle, но рендер работал с копией
    // объекта и записывал угол в неё — сглаживание не накапливалось вообще.
    // Доступ только из потока рендера.
    static std::map<int32_t, float> s_rotation_cache;
    float& cached_rotation = s_rotation_cache[vehicle.id];

    // Constant on-screen size: world-space vertices shrink as the camera
    // zooms in, so the marker never covers the track at high zoom.
    const float markerScale = 1.0f / (camera_zoom > 0.01f ? camera_zoom : 0.01f);
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
    const std::vector<glm::vec2>& outlineShape = vehicle.is_leader ? triangleOutline : circleOutline;
    const std::vector<glm::vec2>& bodyShape = vehicle.is_leader ? triangleBody : circleBody;

    // ========================================================================
    // ✅ CALCULATE ROTATION ANGLE WITH PERSISTENCE
    // Caches last valid angle to prevent flickering when GPS jitter causes
    // movement < MIN_MOVEMENT threshold. Uses exponential smoothing for gradual rotation.
    // ========================================================================
    float rotationAngle = cached_rotation;  // ✅ Start with cached angle

    if (vehicle.is_leader)
    {
        // Use authoritative heading if available. This is more stable than deriving
        // rotation from frame-to-frame position differences.
        float newAngle = static_cast<float>(vehicle.heading) - glm::half_pi<float>();

        // ✅ SMOOTH INTERPOLATION (exponential smoothing)
        const float SMOOTHING_FACTOR = 0.3f;  // 0.0 = no change, 1.0 = instant (0.3 = good balance)

        // Handle angle wrapping (-PI to PI)
        float angleDiff = newAngle - cached_rotation;

        if (angleDiff > glm::pi<float>())
            angleDiff -= 2.0f * glm::pi<float>();
        else if (angleDiff < -glm::pi<float>())
            angleDiff += 2.0f * glm::pi<float>();

        rotationAngle = cached_rotation + angleDiff * SMOOTHING_FACTOR;

        cached_rotation = rotationAngle;
    }

    // ✅ ОПТИМИЗАЦИЯ: Создаем матрицу вращения один раз
    glm::mat2 rotationMatrix(1.0f);
    if (vehicle.is_leader)
    {
        float cosAngle = std::cos(rotationAngle);
        float sinAngle = std::sin(rotationAngle);
        rotationMatrix = glm::mat2(
            cosAngle, sinAngle,
            -sinAngle, cosAngle
        );
    }

    const glm::vec2 renderOffset = vehicle.apply_track_render_offset ? getTrackRenderOffset() : glm::vec2(0.0f, 0.0f);
    const float baseX = static_cast<float>(vehicle.x) + renderOffset.x;
    const float baseY = static_cast<float>(vehicle.y) + renderOffset.y;

    // === РИСУЕМ БЕЛУЮ ОБВОДКУ ===
    std::vector<glm::vec2> outlineVertices;
    outlineVertices.reserve(outlineShape.size());
    for (const auto& vertex : outlineShape) {
        // ✅ Применяем матрицу поворота (экономит вычисления cos/sin)
        glm::vec2 transformedVertex = (vehicle.is_leader)
            ? rotationMatrix * vertex
            : vertex;
        transformedVertex *= markerScale;

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
        glm::vec2 transformedVertex = (vehicle.is_leader)
            ? rotationMatrix * vertex
            : vertex;
        transformedVertex *= markerScale;

        bodyVertices.push_back(glm::vec2(
           transformedVertex.x + baseX,
            transformedVertex.y + baseY
        ));
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, bodyVertices.size() * sizeof(glm::vec2),
        bodyVertices.data(), GL_DYNAMIC_DRAW);

    // ✅ Используем кешированный цвет машины
    glUniform3f(colorLoc, vehicle.color.r, vehicle.color.g, vehicle.color.b);

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

    // Читаем ОПУБЛИКОВАННЫЙ СНИМОК, а не рабочее состояние пайплайна. Отсюда
    // сразу два свойства: кадр не может застать половину перестройки при
    // перемотке повтора, и отрисовка вообще не соперничает с приёмом пакетов за
    // мьютекс — раньше он захватывался каждый кадр. См. core/WorldSnapshot.h.
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();

    std::vector<VehicleRenderState> vehiclesToRender;
    vehiclesToRender.reserve(snapshot->vehicles.size());

    for (const auto& [id, view] : snapshot->vehicles) {
        // Связи нет — координаты застыли. Точку не рисуем, чтобы оператор не
        // принял её за едущую машину; в таблице участник остаётся со своими
        // кругами (см. removeVehicles / g_race_session_active).
        if (view.signal_lost)
            continue;

        VehicleRenderState state;
        state.id = id;
        state.x = view.x;
        state.y = view.y;
        state.heading = view.heading;
        state.speed_kph = view.speed_kph;
        state.color = view.color;
        state.name = view.name;
        state.is_leader = view.is_leader;
        state.apply_track_render_offset = view.apply_track_render_offset;
        vehiclesToRender.push_back(std::move(state));
    }

    // Сглаживание применяем ТОЛЬКО когда данные идут в реальном темпе. На паузе
    // и при перемотке позиции из снимка уже точные, а интерполятор в этот
    // момент опирается на пачку пакетов с чужими метками времени и уводит
    // машину в сторону — те самые остаточные прыжки.
    if (g_position_smoothing_enabled.load(std::memory_order_relaxed))
    {
        for (VehicleRenderState& state : vehiclesToRender)
        {
            double interp_x = 0.0;
            double interp_y = 0.0;
            double interp_heading = 0.0;
            double interp_speed = 0.0;
            if (VehicleInterpolator::Get().GetInterpolatedState(
                    state.id, renderTime, interp_x, interp_y, interp_heading, interp_speed))
            {
                state.x = interp_x;
                state.y = interp_y;
                state.heading = interp_heading;
                state.speed_kph = interp_speed;
            }
        }
    }

    const auto is_visible = [&](const VehicleRenderState& state) {
        return state.x >= minX && state.x <= maxX && state.y >= minY && state.y <= maxY;
    };

    // ✅ Рендеринг БЕЗ блокировки (может занять 10-20ms)
    for (const VehicleRenderState& state : vehiclesToRender) {
        if (is_visible(state))
            renderVehicle(shader_program, vao, vbo, state, projection, camera_zoom);
    }

    // Draw TLA names above each vehicle if enabled.
    // Apply the same track-centering offset used by the dot so label and
    // dot always land at the same screen position.
    if (g_show_vehicle_names) {
        for (const VehicleRenderState& state : vehiclesToRender) {
            if (state.name.empty() || !is_visible(state))
                continue;

            const glm::vec2 rOff = state.apply_track_render_offset
                                     ? getTrackRenderOffset()
                                     : glm::vec2(0.0f, 0.0f);
            VehicleNameRenderer::DrawName(
                state.name,
                static_cast<float>(state.x) + rOff.x,
                static_cast<float>(state.y) + rOff.y,
                projection, g_ui ? g_ui->GetTitleFont() : nullptr, 1.0f);
        }
    }
}

void vehicleClose()
{
	g_is_vehicles_active = false;
}