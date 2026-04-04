#include "../network/SimulationServer.h"
#include "../network/ESP32_Code.h"
#include "../network/Server.h"
#include "../vehicle/Vehicle.h"
#include "../vehicle/VehicleInterpolator.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"
#include <random>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <limits>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <GeographicLib/UTMUPS.hpp>  // For accurate GPS conversion
#include "../../UI.h"

extern UI* g_ui;

// ============================================================================
// EXTERNAL GLOBALS
// ============================================================================
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;
extern MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;
extern std::vector<SplinePoint> g_smooth_track_points;
extern std::mutex g_track_mutex;


// ============================================================================
// TIME SYNC: Map per-vehicle telemetry time (packet.time) into local steady-clock
// domain used by VehicleInterpolator. This removes visible jitter caused by using
// packet arrival times as timestamps.
// ============================================================================
uint32_t getMonotonicTimeMs()
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

    struct VehicleLapTimeSmoother {
        bool initialized = false;
        double lastLocalTime = 0.0;
    };

    std::mutex g_lap_smoother_mutex;
    std::unordered_map<int32_t, VehicleLapTimeSmoother> g_lap_smoother;

namespace {
    struct VehicleTimeSync {
        bool initialized = false;
        double offsetSeconds = 0.0; // localTime = packetTime + offset
    };

    std::mutex g_time_sync_mutex;
    std::unordered_map<int32_t, VehicleTimeSync> g_time_sync;
}

double getSynchronizedSnapshotTimeSeconds(int32_t vehicleID, uint32_t sourceTimeMs)
    {
        const double localNow = VehicleInterpolator::GetTime();

        if (sourceTimeMs == 0) {
            return localNow;
        }

        const double packetTimeSeconds = static_cast<double>(sourceTimeMs) / 1000.0;

        std::lock_guard<std::mutex> lock(g_time_sync_mutex);
        VehicleTimeSync& ts = g_time_sync[vehicleID];

        const double sampleOffset = localNow - packetTimeSeconds;

        if (!ts.initialized)
        {
            ts.initialized = true;
            ts.offsetSeconds = sampleOffset;
        }
        else
        {
            // If the source clock jumped (device reboot / midnight reset), re-sync.
            const double predictedLocal = packetTimeSeconds + ts.offsetSeconds;
            if (std::abs(predictedLocal - localNow) > 5.0)
            {
                ts.offsetSeconds = sampleOffset;
            }
            else
            {
                // Smooth offset to reduce noise without adding latency.
                ts.offsetSeconds = ts.offsetSeconds * 0.98 + sampleOffset * 0.02;
            }
        }

            return packetTimeSeconds + ts.offsetSeconds;
        }

namespace {
    // ------------------------------------------------------------------------
    // Track progress: compute 0..1 progress from current normalized position by
    // projecting onto the closest track segment (server and client will match
    // as long as they share the same `g_smooth_track_points`).
    // ------------------------------------------------------------------------
    struct TrackProgressCache {
        size_t pointCount = 0;
        std::vector<float> cumulativeDistances;
        float totalLength = 0.0f;
    };

    std::mutex g_track_progress_mutex;
    TrackProgressCache g_track_progress_cache;

    void ensureTrackProgressCacheLocked(const std::vector<SplinePoint>& track)
    {
        if (track.size() == g_track_progress_cache.pointCount && g_track_progress_cache.totalLength > 1e-6f) {
            return;
        }

        g_track_progress_cache.pointCount = track.size();
        g_track_progress_cache.cumulativeDistances.clear();
        g_track_progress_cache.totalLength = 0.0f;

        if (track.size() < 2) {
            return;
        }

        g_track_progress_cache.cumulativeDistances.reserve(track.size());
        g_track_progress_cache.cumulativeDistances.push_back(0.0f);

        float total = 0.0f;
        for (size_t i = 1; i < track.size(); ++i)
        {
            const float seg = glm::distance(track[i - 1].position, track[i].position);
            total += seg;
            g_track_progress_cache.cumulativeDistances.push_back(total);
        }

        g_track_progress_cache.totalLength = total;
    }

    double calculateTrackProgressFromPosition(double x, double y)
    {
        if (!g_is_map_loaded) {
            return 0.0;
        }

        std::vector<SplinePoint> trackCopy;
        {
            std::lock_guard<std::mutex> lock(g_track_mutex);
            trackCopy = g_smooth_track_points;
        }

        if (trackCopy.size() < 2) {
            return 0.0;
        }

        std::lock_guard<std::mutex> lock(g_track_progress_mutex);
        ensureTrackProgressCacheLocked(trackCopy);

        if (g_track_progress_cache.totalLength <= 1e-6f) {
            return 0.0;
        }

        const glm::vec2 p(static_cast<float>(x), static_cast<float>(y));

        double bestDistSq = std::numeric_limits<double>::infinity();
        double bestDistanceAlong = 0.0;

        const size_t segmentCount = trackCopy.size() - 1;
        for (size_t i = 0; i < segmentCount; ++i)
        {
            const glm::vec2 a = trackCopy[i].position;
            const glm::vec2 b = trackCopy[i + 1].position;
            const glm::vec2 ab = b - a;
            const float abLenSq = glm::dot(ab, ab);
            if (abLenSq < 1e-10f) {
                continue;
            }

            const float t = glm::clamp(glm::dot(p - a, ab) / abLenSq, 0.0f, 1.0f);
            const glm::vec2 proj = a + ab * t;
            const glm::vec2 d = p - proj;
            const double distSq = static_cast<double>(glm::dot(d, d));

            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                const float segLen = std::sqrt(abLenSq);
                bestDistanceAlong = static_cast<double>(g_track_progress_cache.cumulativeDistances[i]) + static_cast<double>(segLen * t);
            }
        }

        double progress = bestDistanceAlong / static_cast<double>(g_track_progress_cache.totalLength);
        progress = std::clamp(progress, 0.0, 1.0);
        return progress;
    }

    void applyRaceStateFromPacket(Vehicle& vehicle, const VehicleStatePacket& packet)
    {
        vehicle.m_track_progress = packet.track_progress;
        vehicle.m_current_lap_timer = packet.current_lap_time;
     // Keep server-provided last lap time in lap table so UI can read it.
        if (packet.last_lap_time >= 0.0f)
        {
            const int prevLap = packet.current_lap_number - 1;
            if (prevLap >= RaceConstants::LAP_START_NUMBER)
            {
                vehicle.m_laps[prevLap] = LapData(packet.last_lap_time, 0);
            }
        }
        vehicle.m_best_lap_time = packet.best_lap_time;
        vehicle.m_completed_laps = packet.completed_laps;
        vehicle.m_current_lap_number = packet.current_lap_number;
        vehicle.m_has_started_first_lap = (packet.has_started_first_lap != 0);
        vehicle.m_is_leader = (packet.is_leader != 0);
        vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
        vehicle.m_has_authoritative_state = true;
    }
} // namespace

        void fillPacketRaceStateFromVehicle(VehicleStatePacket& packet, const Vehicle& vehicle)
        {
            packet.current_lap_time = vehicle.m_current_lap_timer;
     // Previous lap time (if completed at least one lap)
        {
            float prev = -1.0f;
            const int prevLap = vehicle.m_current_lap_number - 1;
            auto it = vehicle.m_laps.find(prevLap);
            if (it != vehicle.m_laps.end())
                prev = it->second.lapTime;
            packet.last_lap_time = prev;
        }
            packet.best_lap_time = vehicle.m_best_lap_time;
            packet.completed_laps = vehicle.m_completed_laps;
            packet.current_lap_number = vehicle.m_current_lap_number;
            packet.has_started_first_lap = vehicle.m_has_started_first_lap ? 1 : 0;
            packet.is_leader = vehicle.m_is_leader ? 1 : 0;
        }


// ============================================================================
// UNIFIED TELEMETRY PROCESSING - Single entry point for all data sources
// Used by: simulation, real COM port, network clients
// ============================================================================
void processIncomingTelemetry(const TelemetryPacket& packet)
{
    if (packet.MagicMarker != PACKET_MAGIC_DATA && packet.MagicMarker != PacketMagic::DATA)
    {
        return;
    }

    // Map prototype/device IDs (coming from hardware) to race vehicle IDs (1..N)
    // This decouples device identity from race identity.
    static std::mutex s_proto_map_mutex;
    static std::unordered_map<int32_t, int32_t> s_proto_to_race_id;
    static int32_t s_next_race_id = 1;

    // Ignore telemetry until a track is loaded.
    // COM port can stay connected, but we don't create/update vehicles without the map origin.
    // NOTE: Prototype->race assignment still happens so UI can show "Connected" immediately.
    if (!g_is_map_loaded)
    {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true))
        {
            std::cerr << "[TELEMETRY] Ignoring telemetry updates: map/track is not loaded yet." << std::endl;
        }
        return;
    }

    int32_t raceID;
    {
        std::lock_guard<std::mutex> lock(s_proto_map_mutex);
        auto it = s_proto_to_race_id.find(packet.ID);
        if (it == s_proto_to_race_id.end())
        {
            raceID = s_next_race_id++;
            s_proto_to_race_id.emplace(packet.ID, raceID);
            std::cout << "[TELEMETRY] Prototype #" << packet.ID << " assigned race vehicle #" << raceID << std::endl;
            if (g_ui) {
                g_ui->NotifyPrototypeConnected(raceID);
            }
        }
        else
        {
            raceID = it->second;
        }
    }

    // If origin isn't initialized yet (zone + UTM origin), GPS->UTM conversion can fail.
    // This would collapse vehicle positions to (0,0) and look like a constant render offset.
    {
        const int zone = g_map_origin.m_origin_zone_int;
        const bool origin_ok = (zone >= 1 && zone <= 60) &&
            (std::abs(g_map_origin.m_origin_meters_easting) > 1.0) &&
            (std::abs(g_map_origin.m_origin_meters_northing) > 1.0);

        if (!origin_ok)
        {
            static std::atomic<bool> warnedOrigin{ false };
            if (!warnedOrigin.exchange(true))
            {
                std::cerr << "[TELEMETRY] Ignoring telemetry: map origin not initialized yet (zone/UTM origin invalid)." << std::endl;
            }
            return;
        }
    }

    // ? Debug: print packet info to diagnose coordinate issues
    static int packet_count = 0;
    packet_count++;
    if (packet_count % 60 == 0) { // Log every 60th packet (once per second at ~60Hz)
        // Arduino packs GPS as scaled integers: degrees * 1e7
        const double lat_deg = static_cast<double>(packet.lat) / 1e7;
        const double lon_deg = static_cast<double>(packet.lon) / 1e7;
        std::cout << "[TELEMETRY DEBUG] Prototype ID=" << packet.ID << " -> Vehicle #" << raceID
               << " | GPS: (" << lat_deg << ", " << lon_deg << ")"
                  << " | Speed: " << (packet.speed / 100.0) << " km/h" << std::endl;
    }

    // ? CRITICAL DEBUG: Check vehicle existence BEFORE lock
    bool vehicle_exists = false;
    {
        std::lock_guard<std::mutex> check_lock(g_vehicles_mutex);
        vehicle_exists = (g_vehicles.find(raceID) != g_vehicles.end());

        // Print map contents on creation
        if (!vehicle_exists && packet_count % 10 == 0) {
            std::cout << "[DEBUG] Vehicle #" << raceID << " NOT in map. Current: ";
            for (const auto& [id, v] : g_vehicles) {
                std::cout << "#" << id << " ";
            }
            std::cout << "(total: " << g_vehicles.size() << ")" << std::endl;
        }
    }

    // 1) Update authoritative server-side vehicle state from telemetry.
    // Also replicate at a bounded rate to avoid UI jitter from uneven serial packet timing.
    static std::mutex s_send_rate_mutex;
    static std::unordered_map<int32_t, uint32_t> s_last_send_time_ms;
    const uint32_t now_ms = getMonotonicTimeMs();
    constexpr uint32_t kMinSendIntervalMs = 16; // ~60 Hz
    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);

        auto it = g_vehicles.find(raceID);

        if (it != g_vehicles.end())
        {
            // ? UPDATE existing vehicle
            Vehicle& vehicle = it->second;

            // ? CRITICAL: Save old position BEFORE updating coordinates
            // This allows RaceManager to detect line crossing and renderer to calculate direction
            vehicle.m_prev_x = vehicle.m_normalized_x;
            vehicle.m_prev_y = vehicle.m_normalized_y;
            vehicle.m_prev_track_progress = vehicle.m_track_progress;

            vehicle.m_lat_dd = packet.lat / 1e7;
            vehicle.m_lon_dd = packet.lon / 1e7;
            vehicle.m_speed_kph = packet.speed / 100.0;
            vehicle.m_acceleration = packet.acceleration / 100.0;
            vehicle.m_g_force_x = packet.gForceX / 100.0;
            vehicle.m_g_force_y = packet.gForceY / 100.0;

            coordinatesToMeters(vehicle.m_lat_dd, vehicle.m_lon_dd, 
                               vehicle.m_meters_easting, vehicle.m_meters_northing);
            getCoordinateDifferenceFromOrigin(vehicle.m_meters_easting, vehicle.m_meters_northing,
                                             vehicle.m_normalized_x, vehicle.m_normalized_y);

            // [DEBUG_ALIGN_TMP] Raw vs render position (once per second)
            if ((packet_count % 60) == 0)
            {
                const glm::vec2 off = getTrackRenderOffset();
                const double rx = vehicle.m_normalized_x + off.x;
                const double ry = vehicle.m_normalized_y + off.y;
                std::cout.setf(std::ios::fixed);
                std::cout << "[DEBUG_ALIGN_TMP] upd proto=" << packet.ID
                    << " race=" << raceID
                    << " utm=(" << std::setprecision(3) << vehicle.m_meters_easting << "," << vehicle.m_meters_northing << ")"
                    << " norm_raw=(" << std::setprecision(6) << vehicle.m_normalized_x << "," << vehicle.m_normalized_y << ")"
                    << " track_off=(" << off.x << "," << off.y << ")"
                    << " norm_render=(" << rx << "," << ry << ")"
                    << std::endl;
            }

            // Update track progress (needed for consistent leader + lap logic on clients)
            vehicle.m_track_progress = calculateTrackProgressFromPosition(vehicle.m_normalized_x, vehicle.m_normalized_y);

            vehicle.m_last_update_time = std::chrono::steady_clock::now();
            vehicle.m_has_authoritative_state = false;

            // ? Calculate heading from movement (only if vehicle moved significantly)
            double dx = vehicle.m_normalized_x - vehicle.m_prev_x;
            double dy = vehicle.m_normalized_y - vehicle.m_prev_y;
            double distance_moved = std::sqrt(dx * dx + dy * dy);

            // Only update heading if vehicle moved at least 1 meter (0.01 in normalized coords)
            double new_heading = vehicle.m_heading; // Keep previous heading by default
            if (distance_moved > 0.01) {
                new_heading = std::atan2(dy, dx);
                vehicle.m_heading = new_heading; // Update stored heading
            }

            // ? Add snapshot to interpolator for smooth rendering
            VehicleSnapshot snapshot;
            snapshot.timestamp = getSynchronizedSnapshotTimeSeconds(raceID, packet.time);
            snapshot.x = vehicle.m_normalized_x;
            snapshot.y = vehicle.m_normalized_y;
            snapshot.speed_kph = vehicle.m_speed_kph;
            snapshot.heading = new_heading;
            snapshot.track_progress = vehicle.m_track_progress;

            VehicleInterpolator::Get().AddSnapshot(raceID, snapshot);

            // Replicate authoritative state to clients (including lap timing produced by RaceManager).
         bool should_send = true;
            {
                std::lock_guard<std::mutex> rlock(s_send_rate_mutex);
                auto& last = s_last_send_time_ms[raceID];
                if (last != 0 && (now_ms - last) < kMinSendIntervalMs)
                    should_send = false;
                else
                    last = now_ms;
            }

            if (should_send)
            {
                VehicleStatePacket state{};
            state.magic_marker = PacketMagic::VSTA;
            state.vehicle_id = raceID;
            state.server_time_ms = (packet.time != 0) ? packet.time : now_ms;
            state.normalized_x = static_cast<float>(vehicle.m_normalized_x);
            state.normalized_y = static_cast<float>(vehicle.m_normalized_y);
            state.heading = static_cast<float>(vehicle.m_heading);
            state.speed_kph = static_cast<float>(vehicle.m_speed_kph);
            state.track_progress = static_cast<float>(vehicle.m_track_progress);
            fillPacketRaceStateFromVehicle(state, vehicle);
            BroadcastVehicleStateToClients(state);
           }
        }
        else
        {
            // ?? Vehicle NOT FOUND - this happens when:
            // 1. Server sends telemetry for vehicle that client doesn't have yet (normal - create it)
            // 2. Vehicle was deleted by timeout (should not recreate)

            #if NETWORKING_ENABLED
            // On CLIENT: Only create vehicle if it doesn't exist (server will send all vehicles)
            // On SERVER: Create vehicle from simulation or real data

            std::cout << "[TELEMETRY] Creating new vehicle #" << raceID << " from prototype #" << packet.ID << std::endl;

            Vehicle new_vehicle(packet);

            // [DEBUG_ALIGN_TMP] Raw vs render position on create
            {
                const glm::vec2 off = getTrackRenderOffset();
                const double rx = new_vehicle.m_normalized_x + off.x;
                const double ry = new_vehicle.m_normalized_y + off.y;
                std::cout.setf(std::ios::fixed);
                std::cout << "[DEBUG_ALIGN_TMP] create proto=" << packet.ID
                    << " race=" << raceID
                    << " utm=(" << std::setprecision(3) << new_vehicle.m_meters_easting << "," << new_vehicle.m_meters_northing << ")"
                    << " norm_raw=(" << std::setprecision(6) << new_vehicle.m_normalized_x << "," << new_vehicle.m_normalized_y << ")"
                    << " track_off=(" << off.x << "," << off.y << ")"
                    << " norm_render=(" << rx << "," << ry << ")"
                    << std::endl;
            }

            // Compute initial track progress (needed for correct leader/standings immediately)
            new_vehicle.m_track_progress = calculateTrackProgressFromPosition(new_vehicle.m_normalized_x, new_vehicle.m_normalized_y);

            // ? CRITICAL: Initialize prev position for first frame
            new_vehicle.m_prev_x = new_vehicle.m_normalized_x;
            new_vehicle.m_prev_y = new_vehicle.m_normalized_y;

            // ? Add initial snapshot BEFORE moving
            VehicleSnapshot snapshot;
            snapshot.timestamp = getSynchronizedSnapshotTimeSeconds(raceID, packet.time);
            snapshot.x = new_vehicle.m_normalized_x;
            snapshot.y = new_vehicle.m_normalized_y;
            snapshot.speed_kph = new_vehicle.m_speed_kph;
            snapshot.heading = 0.0;
            snapshot.track_progress = new_vehicle.m_track_progress;

            new_vehicle.m_has_authoritative_state = false;

            // ? Use emplace to avoid default constructor call!
            auto [insertIt, inserted] = g_vehicles.emplace(raceID, std::move(new_vehicle));

            VehicleInterpolator::Get().AddSnapshot(raceID, snapshot);

            // Replicate initial authoritative state to clients.
            VehicleStatePacket state{};
            state.magic_marker = PacketMagic::VSTA;
            state.vehicle_id = raceID;
            state.server_time_ms = getMonotonicTimeMs();
            state.normalized_x = static_cast<float>(insertIt->second.m_normalized_x);
            state.normalized_y = static_cast<float>(insertIt->second.m_normalized_y);
            state.heading = static_cast<float>(insertIt->second.m_heading);
            state.speed_kph = static_cast<float>(insertIt->second.m_speed_kph);
            state.track_progress = static_cast<float>(insertIt->second.m_track_progress);
            fillPacketRaceStateFromVehicle(state, insertIt->second);
            BroadcastVehicleStateToClients(state);
            #else
            // Without networking, ignore unknown vehicles
            std::cerr << "[TELEMETRY WARNING] Ignoring packet for unknown vehicle #" << raceID << std::endl;
            #endif
        }
    }

    // Network replication is server-authoritative via VehicleStatePacket.
    // Do not broadcast raw telemetry to clients.
}

void processIncomingVehicleState(const VehicleStatePacket& packet)
{
    if (packet.magic_marker != PacketMagic::VSTA)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_vehicles_mutex);

    auto it = g_vehicles.find(packet.vehicle_id);
    if (it != g_vehicles.end())
    {
        Vehicle& vehicle = it->second;

        vehicle.m_prev_x = vehicle.m_normalized_x;
        vehicle.m_prev_y = vehicle.m_normalized_y;
        vehicle.m_prev_track_progress = vehicle.m_track_progress;

        vehicle.m_normalized_x = packet.normalized_x;
        vehicle.m_normalized_y = packet.normalized_y;
        vehicle.m_speed_kph = packet.speed_kph;
        vehicle.m_heading = packet.heading;
        vehicle.m_last_update_time = std::chrono::steady_clock::now();

        applyRaceStateFromPacket(vehicle, packet);

        // Smooth current lap timer locally between packets for UI.
        {
            const double now = VehicleInterpolator::GetTime();
            std::lock_guard<std::mutex> slock(g_lap_smoother_mutex);
            auto& sm = g_lap_smoother[packet.vehicle_id];
            if (!sm.initialized)
            {
                sm.initialized = true;
                sm.lastLocalTime = now;
            }
            else
            {
                const double dt = std::clamp(now - sm.lastLocalTime, 0.0, 0.25);
                sm.lastLocalTime = now;
                // Only advance if lap has started and server didn't just reset time.
                if (vehicle.m_has_started_first_lap && vehicle.m_current_lap_timer >= 0.0f)
                    vehicle.m_current_lap_timer += static_cast<float>(dt);
            }
        }

        VehicleSnapshot snapshot;
        snapshot.timestamp = getSynchronizedSnapshotTimeSeconds(packet.vehicle_id, packet.server_time_ms);
        snapshot.x = vehicle.m_normalized_x;
        snapshot.y = vehicle.m_normalized_y;
        snapshot.speed_kph = vehicle.m_speed_kph;
        snapshot.heading = vehicle.m_heading;
        snapshot.track_progress = vehicle.m_track_progress;
        VehicleInterpolator::Get().AddSnapshot(packet.vehicle_id, snapshot);
    }
    else
    {
        Vehicle new_vehicle(packet.vehicle_id, packet.normalized_x, packet.normalized_y);
        new_vehicle.m_prev_x = packet.normalized_x;
        new_vehicle.m_prev_y = packet.normalized_y;
        new_vehicle.m_heading = packet.heading;
        new_vehicle.m_speed_kph = packet.speed_kph;
        new_vehicle.m_last_update_time = std::chrono::steady_clock::now();

        applyRaceStateFromPacket(new_vehicle, packet);

        VehicleSnapshot snapshot;
        snapshot.timestamp = getSynchronizedSnapshotTimeSeconds(packet.vehicle_id, packet.server_time_ms);
        snapshot.x = new_vehicle.m_normalized_x;
        snapshot.y = new_vehicle.m_normalized_y;
        snapshot.speed_kph = new_vehicle.m_speed_kph;
        snapshot.heading = new_vehicle.m_heading;
        snapshot.track_progress = new_vehicle.m_track_progress;

        g_vehicles.emplace(packet.vehicle_id, std::move(new_vehicle));
        VehicleInterpolator::Get().AddSnapshot(packet.vehicle_id, snapshot);
    }

}


// ============================================================================

// 1. Calculate cumulative distances along track
static std::pair<std::vector<float>, float> calculateCumulativeDistances(
    const std::vector<SplinePoint>& track)
{
    std::vector<float> cumulative_distances;
    cumulative_distances.reserve(track.size());
    cumulative_distances.push_back(0.0f);

    float total_length = 0.0f;
    for (size_t i = 1; i < track.size(); i++)
    {
        float segment_length = glm::distance(track[i].position, track[i - 1].position);
        total_length += segment_length;
        cumulative_distances.push_back(total_length);
    }

    return {cumulative_distances, total_length};
}

// 2. Update vehicle physics (speed, distance)
static void updateVehiclePhysics(
    float& currentDistance,
    double& currentSpeedKph,
    float& timeSinceLastSpeedChange,
    float& timeUntilSpeedChange,
    float deltaTime,
    float total_track_length,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& speed_dist,
    std::uniform_real_distribution<float>& duration_dist)
{
    // Speed change logic
    timeSinceLastSpeedChange += deltaTime;
    if (timeSinceLastSpeedChange >= timeUntilSpeedChange)
    {
        currentSpeedKph = speed_dist(rng);
        timeUntilSpeedChange = duration_dist(rng);
        timeSinceLastSpeedChange = 0.0f;
    }

    // Calculate distance
    double speedMetersPerSecond = (currentSpeedKph * 1000.0) / 3600.0;
    double speedNormalizedPerSecond = speedMetersPerSecond / MapConstants::MAP_SIZE;
    float distanceThisFrame = static_cast<float>(speedNormalizedPerSecond * deltaTime);

    currentDistance += distanceThisFrame;

    // Cyclic movement
    if (currentDistance >= total_track_length)
    {
        currentDistance = std::fmod(currentDistance, total_track_length);
    }
}

// 3. Sample authoritative state on track
struct TrackSample
{
    glm::vec2 position{ 0.0f, 0.0f };
    glm::vec2 tangent{ 1.0f, 0.0f };
};

static TrackSample sampleTrackState(
    float currentDistance,
    const std::vector<SplinePoint>& track,
    const std::vector<float>& cumulative_distances)
{
    TrackSample sample;

    size_t segment_index = 0;
    for (size_t i = 1; i < cumulative_distances.size(); i++)
    {
        if (cumulative_distances[i] >= currentDistance)
        {
            segment_index = i - 1;
            break;
        }
    }

    if (segment_index >= track.size() - 1)
    {
        segment_index = track.size() - 2;
    }

    float segment_start = cumulative_distances[segment_index];
    float segment_end = cumulative_distances[segment_index + 1];
    float segment_length = segment_end - segment_start;

    float local_fraction = 0.0f;
    if (segment_length > 1e-6f)
    {
        local_fraction = (currentDistance - segment_start) / segment_length;
        local_fraction = glm::clamp(local_fraction, 0.0f, 1.0f);
    }

    sample.position = glm::mix(
        track[segment_index].position,
        track[segment_index + 1].position,
        local_fraction
    );

    glm::vec2 mixedTangent = glm::mix(
        track[segment_index].tangent,
        track[segment_index + 1].tangent,
        local_fraction
    );

    if (glm::length(mixedTangent) > 1e-6f)
    {
        sample.tangent = glm::normalize(mixedTangent);
    }
    else if (glm::length(track[segment_index].tangent) > 1e-6f)
    {
        sample.tangent = glm::normalize(track[segment_index].tangent);
    }

    return sample;
}

// 4. Convert normalized coordinates to GPS
static bool convertNormalizedToGPS(
    const glm::vec2& normalized_pos,
    double& out_latitude,
    double& out_longitude)
{
    double meters_easting = g_map_origin.m_origin_meters_easting + 
                           (normalized_pos.x * MapConstants::MAP_SIZE);
    double meters_northing = g_map_origin.m_origin_meters_northing + 
                            (normalized_pos.y * MapConstants::MAP_SIZE);

    try {
        using namespace GeographicLib;
        // ? CRITICAL: Use origin's hemisphere, not hardcoded 'true'
        bool northp = (g_map_origin.m_origin_zone_char >= 'N');
        UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp,
                       meters_easting, meters_northing,
                       out_latitude, out_longitude);
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[GPS] Conversion error: " << e.what() << std::endl;
        return false;
    }
}

// 5. Create telemetry packet
static TelemetryPacket createTelemetryPacket(
    int vehicle_id,
    double latitude,
    double longitude,
    double speed_kph,
    float track_progress)
{
    TelemetryPacket packet;
    packet.MagicMarker = PACKET_MAGIC_DATA;
    packet.ID = vehicle_id;
    packet.lat = static_cast<int32_t>(latitude * 1e7);
    packet.lon = static_cast<int32_t>(longitude * 1e7);
    packet.speed = static_cast<uint16_t>(speed_kph * 100.0);
    packet.acceleration = 0;
    packet.gForceX = 0;
    packet.gForceY = 0;
    packet.fixtype = 4;
    // Use a monotonic time source so client-side interpolation and lap timing can
    // be stable even for simulated vehicles.
    packet.time = getMonotonicTimeMs();

    return packet;
}

static VehicleStatePacket createVehicleStatePacket(
    int vehicle_id,
    const glm::vec2& normalized_pos,
    const glm::vec2& tangent,
    double speed_kph,
    float track_progress)
{
    VehicleStatePacket packet{};
    packet.magic_marker = PacketMagic::VSTA;
    packet.vehicle_id = vehicle_id;
    packet.server_time_ms = getMonotonicTimeMs();
    packet.normalized_x = normalized_pos.x;
    packet.normalized_y = normalized_pos.y;
    packet.heading = std::atan2(tangent.y, tangent.x);
    packet.speed_kph = static_cast<float>(speed_kph);
    packet.track_progress = track_progress;
    packet.current_lap_time = 0.0f;
    packet.best_lap_time = -1.0f;
    packet.completed_laps = 0;
    packet.current_lap_number = RaceConstants::LAP_START_NUMBER;
    packet.has_started_first_lap = 0;
    packet.is_leader = 0;
    return packet;
}

// ============================================================================
// SIMULATION WORKER (coordinates tasks)
// ============================================================================
// Simulation worker function (runs in separate thread) - ??????????? ????????
static void simulationThreadWorker(int vehicle_id, std::vector<SplinePoint> smooth_path)
{
    std::cout << "[SIM] Vehicle #" << vehicle_id << " started (track points: " << smooth_path.size() << ")" << std::endl;

    // ? 1. Calculate cumulative distances (once)
    auto [cumulative_distances, total_track_length] = calculateCumulativeDistances(smooth_path);

    if (total_track_length < 1e-6f)
    {
        std::cerr << "[SIM] Error: Track length is zero!" << std::endl;
        return;
    }

    std::cout << "[SIM] Vehicle #" << vehicle_id << " track length: " << total_track_length << " units" << std::endl;

    // ? 2. Initialize random number generator
    std::mt19937 gen(vehicle_id * 12345 + static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<double> speed_dist(20.0, 100.0);
    std::uniform_real_distribution<float> segment_duration_dist(3.0f, 8.0f);

    // ? 3. Initialize physics state
    double currentSpeedKph = speed_dist(gen);
    float timeUntilSpeedChange = segment_duration_dist(gen);
    float timeSinceLastSpeedChange = 0.0f;
    float currentDistance = 0.0f;

    const float update_interval_ms = SimulationConstants::UPDATE_INTERVAL_MS;
    const float deltaTime = update_interval_ms / 1000.0f;

    std::cout << "[SIM] Vehicle #" << vehicle_id << " initial speed: " << currentSpeedKph << " km/h" << std::endl;

    // ? 4. Main simulation loop
    while (true)
    {
        // Vehicle will be created by first telemetry packet via processIncomingTelemetry()
        // No need to check if it exists here - simulation keeps running

        // ? Update physics
        updateVehiclePhysics(
            currentDistance,
            currentSpeedKph,
            timeSinceLastSpeedChange,
            timeUntilSpeedChange,
            deltaTime,
            total_track_length,
            gen,
            speed_dist,
            segment_duration_dist
        );

        // ? Sample authoritative track state
        TrackSample sample = sampleTrackState(
            currentDistance,
            smooth_path,
            cumulative_distances
        );

        // ? Calculate track progress (for RaceManager)
        float track_progress = currentDistance / total_track_length;

        // ? Create processed authoritative state packet
        VehicleStatePacket packet = createVehicleStatePacket(
            vehicle_id,
            sample.position,
            sample.tangent,
            currentSpeedKph,
            track_progress
        );

        // ? Update local authoritative server state exactly, without GPS roundtrip
        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            auto it = g_vehicles.find(vehicle_id);

            if (it == g_vehicles.end())
            {
                Vehicle new_vehicle(vehicle_id, sample.position.x, sample.position.y);
                new_vehicle.m_prev_x = sample.position.x;
                new_vehicle.m_prev_y = sample.position.y;
                new_vehicle.m_heading = packet.heading;
                new_vehicle.m_speed_kph = currentSpeedKph;
                new_vehicle.m_track_progress = track_progress;
                new_vehicle.m_prev_track_progress = track_progress;
                new_vehicle.m_has_authoritative_state = false;
                new_vehicle.m_last_update_time = std::chrono::steady_clock::now();
                auto [insertedIt, inserted] = g_vehicles.emplace(vehicle_id, std::move(new_vehicle));
                it = insertedIt;
            }
            else
            {
                Vehicle& vehicle = it->second;
                vehicle.m_prev_x = vehicle.m_normalized_x;
                vehicle.m_prev_y = vehicle.m_normalized_y;
                vehicle.m_prev_track_progress = vehicle.m_track_progress;
                vehicle.m_normalized_x = sample.position.x;
                vehicle.m_normalized_y = sample.position.y;
                vehicle.m_heading = packet.heading;
                vehicle.m_speed_kph = currentSpeedKph;
                vehicle.m_track_progress = track_progress;
                vehicle.m_has_authoritative_state = false;
                vehicle.m_last_update_time = std::chrono::steady_clock::now();
            }

            fillPacketRaceStateFromVehicle(packet, it->second);

            VehicleSnapshot snapshot;
            snapshot.timestamp = getSynchronizedSnapshotTimeSeconds(packet.vehicle_id, packet.server_time_ms);
            snapshot.x = sample.position.x;
            snapshot.y = sample.position.y;
            snapshot.speed_kph = currentSpeedKph;
            snapshot.heading = packet.heading;
            snapshot.track_progress = track_progress;
            VehicleInterpolator::Get().AddSnapshot(vehicle_id, snapshot);
        }

        // ? Broadcast processed packet to clients
        BroadcastVehicleStateToClients(packet);

        // Sleep until next update
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(update_interval_ms)));
    }
}

void simulateVehicleMovement(int vehicle_id, const std::vector<SplinePoint>& smooth_track_points)
{
    // Guard clauses
    if (smooth_track_points.empty()) {
        std::cerr << "Error: Empty track points for simulation" << std::endl;
        return;
    }

    if (!g_is_map_loaded) {
        std::cerr << "Error: Map not loaded, cannot simulate vehicle movement" << std::endl;
        return;
    }

    // Calculate total path length for info
    float total_distance = 0.0f;
    for (size_t i = 1; i < smooth_track_points.size(); ++i) {
        total_distance += glm::distance(smooth_track_points[i].position, smooth_track_points[i - 1].position);
    }

    // Launch simulation in separate thread.
    // IMPORTANT: simulation must not reuse the same race IDs as real prototypes.
    // Use a reserved high ID range so the telemetry->race mapping remains stable.
    constexpr int kSimIdBase = 1000000;
    const int sim_vehicle_id = kSimIdBase + vehicle_id;
    std::thread simulation_thread(simulationThreadWorker, sim_vehicle_id, smooth_track_points);
    simulation_thread.detach();
}



