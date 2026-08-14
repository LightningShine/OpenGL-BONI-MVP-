#include "SyntheticTelemetry.h"

#include "TelemetryIngest.h"
#include "../Config.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"

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

    /// Собирает проводной пакет с настоящим CRC и дописывает его в поток.
    void append_packet(std::vector<uint8_t>& stream, uint32_t device_id, uint8_t seq,
                       uint32_t utc_ms, double lat, double lon, double speed_kph,
                       int16_t fix_type)
    {
        rajagp::RajaTelemetryPacket wire{};
        wire.magic = rajagp::PacketMagic::RAJA;
        wire.device_id = device_id;
        wire.seq = seq;
        wire.gps_utc_ms = utc_ms;
        wire.lat = static_cast<int32_t>(lat * 1e7);
        wire.lon = static_cast<int32_t>(lon * 1e7);
        wire.speed = static_cast<uint32_t>(speed_kph * 100.0);
        wire.acceleration = 0;
        wire.gForceX = 0;
        wire.gForceY = 0;
        wire.fix_type = fix_type;
        wire.crc = rajagp::crc16_ccitt_false(reinterpret_cast<const uint8_t*>(&wire),
                                             sizeof(wire) - sizeof(wire.crc));

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&wire);
        stream.insert(stream.end(), bytes, bytes + sizeof(wire));
    }

    struct SyntheticCar
    {
        uint32_t device_id = 0;
        double   speed_kph = 0.0;
        float    distance = 0.0f;   // вдоль трека, нормализованные единицы
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
            cars[i].speed_kph = scenario.base_speed_kph + share * scenario.speed_spread_kph;
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
                const double meters_per_second = car.speed_kph / 3.6;
                car.distance += static_cast<float>(meters_per_second * delta_seconds /
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
                              lat, lon, car.speed_kph,
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
    ingest_start(logging::TelemetryLogSource::Receiver);
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
