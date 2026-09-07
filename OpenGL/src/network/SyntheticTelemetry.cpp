#include "network/SyntheticTelemetry.h"

#include "network/ESP32_Code.h"
#include "network/ReplayPlayer.h"
#include "network/TelemetryIngest.h"
#include "core/Config.h"
#include "input/Input.h"
#include "rendering/Interpolation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <GeographicLib/UTMUPS.hpp>
#include <rajagp/Crc.h>
#include <rajagp/Protocol.h>
#include <rajagp/RajaParser.h>

extern std::vector<SplinePoint> g_smooth_track_points;
extern std::mutex g_track_mutex;
extern MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;

namespace telemetry
{
namespace
{
    // Миллисекунд в сутках: gps_utc_ms считает время от полуночи и переполняется.
    constexpr uint32_t MS_PER_DAY = 86'400'000u;

    // Слот машины внутри такта — грубая имитация TDMA: пакеты машин приходят не
    // одновременно, а по очереди.
    constexpr uint32_t CAR_SLOT_MS = 1;

    // Диапазон синтетических device_id. Отдельно от реальных, чтобы записи
    // синтетики нельзя было спутать с записями железа.
    constexpr uint32_t SYNTHETIC_DEVICE_ID_BASE = 9000;

    constexpr size_t RECORD_SIZE = sizeof(rajagp::RajaTelemetryPacket);   // 37
    constexpr size_t MAGIC_SIZE  = sizeof(uint32_t);                      // 4

    std::atomic<bool> g_running{ false };
    std::atomic<bool> g_stop_requested{ false };
    std::thread g_thread;

    /// Снимок трека в кадре трека плюс накопленные длины (нормализованные единицы).
    struct TrackPath
    {
        std::vector<SplinePoint> points;
        std::vector<float> cumulative;
        float total_length = 0.0f;
    };

    /// Готовит накопленные длины для трека. Возвращает false, если геометрии нет.
    bool build_path(const std::vector<SplinePoint>& track, TrackPath& path)
    {
        path.points = track;
        if (path.points.size() < 2)
            return false;

        path.cumulative.reserve(path.points.size());
        path.cumulative.push_back(0.0f);
        float total = 0.0f;
        for (size_t i = 1; i < path.points.size(); ++i)
        {
            total += glm::distance(path.points[i - 1].position, path.points[i].position);
            path.cumulative.push_back(total);
        }
        path.total_length = total;
        return total > 1e-6f;
    }

    /// Точка и касательная на расстоянии `distance` вдоль трека (с заворотом).
    void sample_path(const TrackPath& path, float distance, glm::vec2& out_position,
                     glm::vec2& out_tangent)
    {
        float wrapped = std::fmod(distance, path.total_length);
        if (wrapped < 0.0f)
            wrapped += path.total_length;

        const auto it = std::upper_bound(path.cumulative.begin(), path.cumulative.end(), wrapped);
        size_t index = static_cast<size_t>(std::distance(path.cumulative.begin(), it));
        if (index == 0)
            index = 1;
        if (index >= path.points.size())
            index = path.points.size() - 1;

        const glm::vec2 a = path.points[index - 1].position;
        const glm::vec2 b = path.points[index].position;
        const float segment_length = glm::distance(a, b);
        const float t = (segment_length > 1e-9f)
                            ? (wrapped - path.cumulative[index - 1]) / segment_length
                            : 0.0f;

        out_position = a + (b - a) * std::clamp(t, 0.0f, 1.0f);
        const glm::vec2 direction = b - a;
        const float direction_length = glm::length(direction);
        out_tangent = (direction_length > 1e-9f) ? direction / direction_length
                                                 : glm::vec2(1.0f, 0.0f);
    }

    /// Позиция в кадре трека -> географические координаты, обратно тому пути,
    /// которым реальный пакет превращается в позицию машины
    /// (см. processIncomingTelemetry: lat/lon -> UTM -> нормализованные + offset).
    bool track_point_to_gps(const glm::vec2& track_position, const MapOrigin& origin,
                            const glm::vec2& render_offset, double& out_lat, double& out_lon)
    {
        const double raw_x = static_cast<double>(track_position.x - render_offset.x);
        const double raw_y = static_cast<double>(track_position.y - render_offset.y);

        const double easting = origin.m_origin_meters_easting + raw_x * origin.m_map_size;
        const double northing = origin.m_origin_meters_northing + raw_y * origin.m_map_size;

        try
        {
            const bool northp = (origin.m_origin_zone_char >= 'N');
            GeographicLib::UTMUPS::Reverse(origin.m_origin_zone_int, northp,
                                           easting, northing, out_lat, out_lon);
        }
        catch (const std::exception& error)
        {
            std::cerr << "[SYNTH] UTM reverse failed: " << error.what() << std::endl;
            return false;
        }
        return true;
    }

    // ── Динамика синтетической машины ───────────────────────────────────────
    //
    // Раньше машина шла по трассе с постоянной скоростью, и перегрузки в пакете
    // были нулями: панели перегрузок нечем было проверить, а «идеально ровный»
    // заезд не похож ни на один настоящий. Теперь скорость ограничена
    // поворотом — как её ограничивает сцепление у реальной машины, — и обе
    // перегрузки считаются из движения, а не выдумываются.

    constexpr double G = 9.81;                  // м/с²
    constexpr double MAX_LATERAL_G = 0.9;       // держит поворот, пока не сорвётся
    constexpr double MAX_ACCEL_G   = 0.5;       // разгон
    constexpr double MAX_BRAKE_G   = 0.9;       // торможение
    constexpr double MIN_SPEED_FRACTION = 0.25; // ниже этой доли заданной скорости не тормозим
    // Поперечная перегрузка меряется на участке ДЛИННЕЕ шага полилинии трассы:
    // трасса записана десятками точек, поворот в ней — излом в одной вершине, и
    // на коротком участке кривизна в этом изломе взлетает до значений, которых у
    // машины с её инерцией быть не может. Окно в несколько метров сглаживает
    // излом так же, как его сглаживает сама машина.
    constexpr float  CURVATURE_LOOK_M  = 6.0f;  // на чём меряется текущий поворот
    constexpr float  BRAKING_LOOK_M    = 16.0f; // на сколько вперёд «смотрит водитель»
    constexpr int    BRAKING_SAMPLES   = 8;     // точек в окне торможения

    // Насколько плавно машина возвращается к заданной скорости после поворота.
    // Разгон «на всю» до самой цели и есть источник дребезга: цель чуть уплыла —
    // тяга скачет с полного газа на полный тормоз и обратно.
    constexpr double SPEED_TAU_SECONDS = 0.6;

    // Предел РЫВКА (производной ускорения). Ни водитель, ни машина не меняют
    // тягу мгновенно, а именно мгновенные переключения давали в панели скачки
    // ±0.5 g по несколько раз в секунду при почти постоянной скорости.
    constexpr double MAX_JERK_MPS3 = 6.0;

    /// Поворот пути на участке `look_meters` вперёд, радианы.
    /// Знак — против часовой стрелки (левый поворот), как у atan2.
    float turn_ahead(const TrackPath& path, float distance, float look_meters)
    {
        const float look = look_meters / static_cast<float>(MapConstants::MAP_SIZE);

        glm::vec2 position, tangent, position_ahead, tangent_ahead;
        sample_path(path, distance, position, tangent);
        sample_path(path, distance + look, position_ahead, tangent_ahead);

        const float cross = tangent.x * tangent_ahead.y - tangent.y * tangent_ahead.x;
        const float dot   = tangent.x * tangent_ahead.x + tangent.y * tangent_ahead.y;
        return std::atan2(cross, dot);
    }

    /// Кривизна пути (1/м) на коротком участке впереди.
    double curvature_ahead(const TrackPath& path, float distance, float look_meters)
    {
        return std::fabs(static_cast<double>(turn_ahead(path, distance, look_meters))) /
               static_cast<double>(look_meters);
    }

    /// Поворот на участке ВОКРУГ точки: половина окна назад, половина вперёд.
    /// Симметричное окно нужно, чтобы перегрузка не «включалась» скачком в
    /// момент, когда излом полилинии попадает в поле зрения, и не пропадала так
    /// же резко: машина проходит вершину постепенно, и мерить надо так же.
    float turn_around(const TrackPath& path, float distance, float window_meters)
    {
        const float half = 0.5f * window_meters / static_cast<float>(MapConstants::MAP_SIZE);
        return turn_ahead(path, distance - half, window_meters);
    }

    /// Самый крутой поворот в окне торможения и расстояние до него.
    ///
    /// Берётся МАКСИМУМ, а не среднее: среднее размазывает апекс — длинный
    /// пологий вход гасит короткую шпильку, и машина въезжает в неё, не тормозя.
    /// Расстояние нужно, чтобы тормозить ровно с той силой, которой хватает к
    /// приезду в поворот, а не «в пол» при первом же его появлении в окне.
    struct CornerAhead
    {
        double curvature = 0.0;    // 1/м
        float  distance_m = 0.0f;  // до начала этого участка
    };

    CornerAhead peak_corner_ahead(const TrackPath& path, float distance)
    {
        const float step = BRAKING_LOOK_M / BRAKING_SAMPLES;
        CornerAhead corner;
        for (int i = 0; i < BRAKING_SAMPLES; ++i)
        {
            const float offset = step * i;
            const double curvature = curvature_ahead(path, distance + offset, step);
            if (curvature > corner.curvature)
            {
                corner.curvature = curvature;
                corner.distance_m = offset;
            }
        }
        return corner;
    }

    /// Собирает проводной пакет с настоящим CRC и дописывает его в поток.
    /// Перегрузки идут дополнительным кодом в беззнаковых полях — так их читает
    /// приём (см. Vehicle.cpp), иначе торможение стало бы сотнями g.
    void append_packet(std::vector<uint8_t>& stream, uint32_t device_id, uint8_t seq,
                       uint32_t utc_ms, double lat, double lon, double speed_kph,
                       double accel_mps2, double g_long, double g_lat, int16_t fix_type)
    {
        const auto to_wire_g = [](double value) {
            const double hundredths = std::clamp(value * 100.0, -32000.0, 32000.0);
            return static_cast<uint16_t>(static_cast<int16_t>(std::lround(hundredths)));
        };

        rajagp::RajaTelemetryPacket wire{};
        wire.magic = rajagp::PacketMagic::RAJA;
        wire.device_id = device_id;
        wire.seq = seq;
        wire.gps_utc_ms = utc_ms;
        wire.lat = static_cast<int32_t>(lat * 1e7);
        wire.lon = static_cast<int32_t>(lon * 1e7);
        wire.speed = static_cast<uint32_t>(speed_kph * 100.0);
        wire.acceleration = static_cast<uint32_t>(
            static_cast<int32_t>(std::lround(accel_mps2 * 100.0)));
        wire.gForceX = to_wire_g(g_lat);    // поперечная: + вправо
        wire.gForceY = to_wire_g(g_long);   // продольная: + разгон
        wire.fix_type = fix_type;
        wire.crc = rajagp::crc16_ccitt_false(reinterpret_cast<const uint8_t*>(&wire),
                                             sizeof(wire) - sizeof(wire.crc));

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&wire);
        stream.insert(stream.end(), bytes, bytes + sizeof(wire));
    }

    struct SyntheticCar
    {
        uint32_t device_id = 0;
        double   target_speed_kph = 0.0;   // на прямой, из сценария
        double   speed_mps = 0.0;          // текущая: падает в повороте
        double   accel_mps2 = 0.0;         // текущая тяга/торможение
        float    distance = 0.0f;          // вдоль трека, нормализованные единицы
        uint8_t  seq = 0;
    };

}  // namespace

std::vector<uint8_t> build_synthetic_stream(const SyntheticScenario& scenario,
                                            const std::vector<SplinePoint>& track,
                                            const MapOrigin& origin,
                                            const glm::vec2& render_offset)
{
    std::vector<uint8_t> stream;

    TrackPath path;
    if (!build_path(track, path) || scenario.car_count < 1 || scenario.sample_interval_ms == 0)
        return stream;

    {
        std::mt19937 random(scenario.seed);
        std::uniform_int_distribution<int> loss_roll(1, 100);
        std::uniform_int_distribution<int> jitter_roll(
            -static_cast<int>(scenario.jitter_ms), static_cast<int>(scenario.jitter_ms));

        std::vector<SyntheticCar> cars(static_cast<size_t>(scenario.car_count));
        for (size_t i = 0; i < cars.size(); ++i)
        {
            cars[i].device_id = SYNTHETIC_DEVICE_ID_BASE + static_cast<uint32_t>(i) + 1;
            // Скорости разнесены детерминированно, чтобы машины расходились по
            // трассе и обгоняли друг друга, но прогон оставался повторяемым.
            const double share = (cars.size() > 1)
                                     ? static_cast<double>(i) / static_cast<double>(cars.size() - 1)
                                     : 0.0;
            cars[i].target_speed_kph = scenario.base_speed_kph + share * scenario.speed_spread_kph;
            cars[i].speed_mps = cars[i].target_speed_kph / 3.6;
            // Стартовая расстановка по трассе, как на решётке.
            cars[i].distance = path.total_length *
                               static_cast<float>(i) / static_cast<float>(cars.size()) * 0.05f;
        }

        const uint32_t start_utc_ms =
            scenario.wrap_midnight ? (MS_PER_DAY - 5000u) : 12u * 3600u * 1000u;
        const double delta_seconds = static_cast<double>(scenario.sample_interval_ms) / 1000.0;
        const uint64_t total_ticks =
            static_cast<uint64_t>(scenario.duration_seconds) * 1000ull / scenario.sample_interval_ms;

        const uint32_t gap_start_ms = scenario.gap_start_second * 1000u;
        const uint32_t gap_end_ms = gap_start_ms + scenario.gap_duration_seconds * 1000u;

        stream.reserve(static_cast<size_t>(total_ticks) * cars.size() * RECORD_SIZE);

        for (uint64_t tick = 0; tick < total_ticks; ++tick)
        {
            const uint32_t elapsed_ms = static_cast<uint32_t>(tick * scenario.sample_interval_ms);
            const bool in_gap = (scenario.gap_duration_seconds > 0) &&
                                (elapsed_ms >= gap_start_ms) && (elapsed_ms < gap_end_ms);
            const bool after_gap = (scenario.gap_duration_seconds > 0) && (elapsed_ms >= gap_end_ms);

            for (size_t i = 0; i < cars.size(); ++i)
            {
                SyntheticCar& car = cars[i];

                // Физика считается всегда: потеря пакета не останавливает машину.
                //
                // Скорость держится на заданной, пока позволяет поворот: впереди
                // крутой вираж — машина тормозит до той скорости, на которой
                // поперечная перегрузка ещё в пределах сцепления, за виражом
                // разгоняется обратно. Отсюда берутся обе перегрузки: продольная
                // из изменения скорости, поперечная из скорости и кривизны.
                const double target_mps = car.target_speed_kph / 3.6;
                const CornerAhead corner = peak_corner_ahead(path, car.distance);

                double corner_mps = target_mps;
                if (corner.curvature > 1e-5)
                    corner_mps = std::sqrt(MAX_LATERAL_G * G / corner.curvature);
                corner_mps = std::clamp(corner_mps, target_mps * MIN_SPEED_FRACTION, target_mps);

                // Нужное ускорение: тормозим ровно настолько, чтобы к повороту
                // скорость упала до проходимой (v² = v0² + 2·a·s), а на выходе
                // возвращаемся к заданной плавно, а не полным газом до упора.
                double wanted_accel;
                if (car.speed_mps > corner_mps)
                {
                    const double to_corner = std::fmax(static_cast<double>(corner.distance_m), 1.0);
                    wanted_accel = (corner_mps * corner_mps - car.speed_mps * car.speed_mps) /
                                   (2.0 * to_corner);
                }
                else
                {
                    wanted_accel = (target_mps - car.speed_mps) / SPEED_TAU_SECONDS;
                }
                wanted_accel = std::clamp(wanted_accel, -MAX_BRAKE_G * G, MAX_ACCEL_G * G);

                // Тяга меняется не мгновенно — отсюда гладкая продольная кривая.
                const double jerk_limit = MAX_JERK_MPS3 * delta_seconds;
                car.accel_mps2 += std::clamp(wanted_accel - car.accel_mps2, -jerk_limit, jerk_limit);

                car.speed_mps = std::fmax(0.5, car.speed_mps + car.accel_mps2 * delta_seconds);

                const double accel_mps2 = car.accel_mps2;
                const double g_long = accel_mps2 / G;

                // Поперечная: a = v²·κ. Знак — по стороне поворота, вправо
                // положительный (так подписана шкала панели).
                const double turn = -static_cast<double>(
                    turn_around(path, car.distance, CURVATURE_LOOK_M));
                const double g_lat = (car.speed_mps * car.speed_mps) *
                                     (turn / CURVATURE_LOOK_M) / G;

                car.distance += static_cast<float>(car.speed_mps * delta_seconds /
                                                   MapConstants::MAP_SIZE);

                const bool is_gap_car = (scenario.gap_car_index >= 0) &&
                                        (static_cast<size_t>(scenario.gap_car_index) == i);
                if (is_gap_car && in_gap)
                    continue;  // связь пропала: пакетов нет вообще

                if (scenario.packet_loss_percent > 0 &&
                    loss_roll(random) <= scenario.packet_loss_percent)
                {
                    continue;  // пакет не долетел
                }

                const bool degraded = is_gap_car && after_gap && scenario.degrade_fix_after_gap;

                glm::vec2 position;
                glm::vec2 tangent;
                sample_path(path, car.distance, position, tangent);

                if (degraded && scenario.position_bias_meters != 0.0)
                {
                    // Снос вбок от осевой: именно так теряется пересечение линии
                    // старт/финиша, когда позиция уезжает за её край.
                    const glm::vec2 normal(-tangent.y, tangent.x);
                    const float bias = static_cast<float>(scenario.position_bias_meters /
                                                          MapConstants::MAP_SIZE);
                    position += normal * bias;
                }

                double lat = 0.0;
                double lon = 0.0;
                if (!track_point_to_gps(position, origin, render_offset, lat, lon))
                    continue;

                int64_t stamp = static_cast<int64_t>(start_utc_ms) + elapsed_ms +
                                static_cast<int64_t>(i) * CAR_SLOT_MS;
                if (scenario.jitter_ms > 0)
                    stamp += jitter_roll(random);
                if (stamp < 0)
                    stamp += MS_PER_DAY;

                append_packet(stream, car.device_id, car.seq++,
                              static_cast<uint32_t>(static_cast<uint64_t>(stamp) % MS_PER_DAY),
                              lat, lon, car.speed_mps * 3.6, accel_mps2, g_long, g_lat,
                              degraded ? static_cast<int16_t>(1) : static_cast<int16_t>(4));
            }
        }
    }

    return stream;
}

namespace
{
    /// Проигрывает заранее построенный поток в общую точку приёма, выдерживая
    /// темп сценария. Содержимое пакетов здесь уже не меняется — только момент
    /// подачи, поэтому скорость проигрывания на результат не влияет.
    void player_loop(SyntheticScenario scenario, std::vector<uint8_t> stream)
    {
        const size_t packet_count = stream.size() / RECORD_SIZE;
        const size_t per_tick = static_cast<size_t>(scenario.car_count);

        std::cout << "[SYNTH] Playing " << packet_count << " packets: "
                  << scenario.car_count << " cars, "
                  << (1000u / scenario.sample_interval_ms) << " Hz, "
                  << scenario.duration_seconds << "s, seed=" << scenario.seed
                  << ", speed x" << scenario.speed_multiplier << std::endl;

        // Темп держим по АБСОЛЮТНЫМ дедлайнам, а не сном на фиксированную паузу.
        // Причина конкретная: разрешение таймера Windows ~15.6 мс, поэтому
        // sleep_for(20 мс) спит около 31 мс, и «скорость x1» превращается в x0.65
        // с накоплением отставания. С дедлайнами отставание не копится: если
        // проснулись поздно, следующие такты уходят подряд и темп догоняется.
        const auto started_at = std::chrono::steady_clock::now();
        const double tick_micros = (scenario.speed_multiplier > 0.0)
                                       ? (scenario.sample_interval_ms * 1000.0 /
                                          scenario.speed_multiplier)
                                       : 0.0;

        size_t played = 0;
        for (size_t index = 0; index < packet_count && !g_stop_requested.load(); ++index)
        {
            ingest_wire_packet(stream.data() + index * RECORD_SIZE + MAGIC_SIZE);
            ++played;

            if (tick_micros > 0.0 && per_tick > 0 && ((index + 1) % per_tick) == 0)
            {
                const auto tick_index = static_cast<double>((index + 1) / per_tick);
                const auto deadline = started_at + std::chrono::microseconds(
                    static_cast<long long>(tick_index * tick_micros));
                if (deadline > std::chrono::steady_clock::now())
                    std::this_thread::sleep_until(deadline);
            }
        }

        // Отчитываемся о ФАКТИЧЕСКОМ темпе: расхождение с заданным сразу видно в
        // журнале, а не выясняется потом по размеру файла (именно так был найден
        // промах со sleep_for).
        const double elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
        const double played_seconds =
            (per_tick > 0) ? static_cast<double>(played / per_tick) *
                                 scenario.sample_interval_ms / 1000.0
                           : 0.0;
        std::cout << "[SYNTH] Finished: " << played << " packets, "
                  << played_seconds << "s of scenario in " << elapsed_seconds
                  << "s wall (x" << (elapsed_seconds > 0.0 ? played_seconds / elapsed_seconds : 0.0)
                  << ")" << std::endl;

        ingest_stop();
        g_running.store(false);
    }
}

bool synthetic_start(const SyntheticScenario& scenario)
{
    if (g_running.load())
    {
        std::cout << "[SYNTH] Already running" << std::endl;
        return false;
    }

    if (!g_is_map_loaded)
    {
        std::cerr << "[SYNTH] Cannot start: no track loaded" << std::endl;
        return false;
    }

    // Зеркало проверки в replay_open. Тракт приёма и так отбросит эти пакеты,
    // пока открыт повтор, но генератор при этом открыл бы НОВУЮ запись и писал
    // бы в неё заезд, которого никто не видит.
    if (replay_is_active())
    {
        std::cerr << "[SYNTH] Cannot start: a replay is open, close it first" << std::endl;
        return false;
    }

    // Источник данных в приложении один: два потока пакетов в одном пайплайне
    // смешались бы в одних и тех же машинах.
    if (replay_is_active())
    {
        std::cerr << "[SYNTH] Cannot start: a replay is open, close it first" << std::endl;
        return false;
    }

    if (isRealDataCaptureRunning())
    {
        std::cerr << "[SYNTH] Cannot start: COM capture is running" << std::endl;
        return false;
    }

    const int zone = g_map_origin.m_origin_zone_int;
    if (zone < 1 || zone > 60 || std::abs(g_map_origin.m_origin_meters_easting) < 1.0)
    {
        std::cerr << "[SYNTH] Cannot start: map origin is not initialized" << std::endl;
        return false;
    }

    if (scenario.car_count < 1 || scenario.sample_interval_ms == 0)
    {
        std::cerr << "[SYNTH] Cannot start: invalid scenario" << std::endl;
        return false;
    }

    // Снимок трека берём здесь, под мьютексом, и дальше работаем только с ним:
    // построение потока не должно зависеть от того, что делает рендер.
    std::vector<SplinePoint> track;
    {
        std::lock_guard<std::mutex> lock(g_track_mutex);
        track = g_smooth_track_points;
    }

    std::vector<uint8_t> stream =
        build_synthetic_stream(scenario, track, g_map_origin, getTrackRenderOffset());
    if (stream.empty())
    {
        std::cerr << "[SYNTH] Cannot start: scenario produced no packets" << std::endl;
        return false;
    }

    if (g_thread.joinable())
        g_thread.join();

    g_stop_requested.store(false);
    g_running.store(true);
    // Журнал открывает старт сессии, а не источник — см. RaceManager::StartSession.
    g_thread = std::thread(player_loop, scenario, std::move(stream));
    return true;
}

void synthetic_stop()
{
    g_stop_requested.store(true);
    if (g_thread.joinable())
        g_thread.join();
    g_running.store(false);
}

bool synthetic_is_running()
{
    return g_running.load();
}

}
