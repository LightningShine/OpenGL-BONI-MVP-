#include "../network/ESP32_Code.h"
#include "SimulationServer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <GeographicLib/UTMUPS.hpp>

// External globals from core/vehicle/track systems
extern std::atomic<bool> g_is_map_loaded;
extern std::vector<SplinePoint> g_smooth_track_points;
extern std::mutex g_track_mutex;
extern MapOrigin g_map_origin;

#if defined(_WIN32)
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#pragma comment(lib, "setupapi.lib")
#endif

// ✅ Mode flags (defined in Server.cpp)
extern std::atomic<bool> g_is_server_mode;
extern std::atomic<bool> g_is_client_mode;

serialib serial;

static std::atomic<bool> g_capture_running{ false };
static std::atomic<bool> g_capture_stop_requested{ false };
static std::thread g_capture_thread;
static std::mutex g_serial_mutex;

static std::mutex g_last_packet_mutex;
static TelemetryPacket g_last_packet{};
static std::atomic<bool> g_has_last_packet{ false };

static std::atomic<bool> g_discovery_running{ false };
static std::atomic<bool> g_discovery_stop_requested{ false };
static std::thread g_discovery_thread;

static std::mutex g_ports_mutex;
static std::vector<ComPortInfo> g_ports;
static std::mutex g_selected_port_mutex;
static std::string g_selected_port;

// CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021) — must match device firmware.
static uint16_t raja_crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

static void closeSerialNoThrow()
{
    std::lock_guard<std::mutex> lock(g_serial_mutex);
    // serialib has no isOpen() in all versions; just attempt close.
    try { serial.closeDevice(); }
    catch (...) {}
}

bool calibrateOriginToStartFinish()
{
    if (!g_is_map_loaded)
        return false;

    TelemetryPacket packet{};
    {
        if (!g_has_last_packet.load(std::memory_order_relaxed))
            return false;
        std::lock_guard<std::mutex> lock(g_last_packet_mutex);
        packet = g_last_packet;
    }

    glm::vec2 startPoint(0.0f, 0.0f);
    {
        std::lock_guard<std::mutex> lock(g_track_mutex);
        if (g_smooth_track_points.empty())
            return false;
        startPoint = g_smooth_track_points.front().position;
    }

    // Current telemetry position in UTM meters
    const double lat_deg = static_cast<double>(packet.lat) / 1e7;
    const double lon_deg = static_cast<double>(packet.lon) / 1e7;
    double easting = 0.0;
    double northing = 0.0;
    coordinatesToMeters(lat_deg, lon_deg, easting, northing);

    // Calculate current normalized position BEFORE calibration (for diagnostics)
    double nx_before = 0.0;
    double ny_before = 0.0;
    getCoordinateDifferenceFromOrigin(easting, northing, nx_before, ny_before);

    // Set origin so that this telemetry point maps to the track start point:
    // start = (UTM_current - origin) / MAP_SIZE  => origin = UTM_current - start*MAP_SIZE
    g_map_origin.m_origin_meters_easting = easting - static_cast<double>(startPoint.x) * MapConstants::MAP_SIZE;
    g_map_origin.m_origin_meters_northing = northing - static_cast<double>(startPoint.y) * MapConstants::MAP_SIZE;

    try {
        using namespace GeographicLib;
        const bool northp = (g_map_origin.m_origin_zone_char >= 'N');
        UTMUPS::Reverse(
            g_map_origin.m_origin_zone_int,
            northp,
            g_map_origin.m_origin_meters_easting,
            g_map_origin.m_origin_meters_northing,
            g_map_origin.m_origin_lat_dd,
            g_map_origin.m_origin_lon_dd);
    }
    catch (...) {}

    std::cout << "[ORIGIN] Calibrated to Start/Finish using latest telemetry packet." << std::endl;
    std::cout << "[ORIGIN]   Start point norm=(" << startPoint.x << "," << startPoint.y << ")" << std::endl;
    std::cout << "[ORIGIN]   Telemetry norm BEFORE=(" << nx_before << "," << ny_before << ")" << std::endl;
    std::cout << "[ORIGIN]   New origin UTM: easting=" << g_map_origin.m_origin_meters_easting
              << " northing=" << g_map_origin.m_origin_meters_northing << std::endl;

    // Calculate normalized position AFTER calibration (should match startPoint)
    double nx_after = 0.0;
    double ny_after = 0.0;
    getCoordinateDifferenceFromOrigin(easting, northing, nx_after, ny_after);
    std::cout << "[ORIGIN]   Telemetry norm AFTER =(" << nx_after << "," << ny_after << ")" << std::endl;

    return true;
}

bool openCOMPort(const std::string& port_name)
{
    std::lock_guard<std::mutex> lock(g_serial_mutex);

    // On Windows, COM ports must be opened as "\\\\.\\COMx" for COM10+.
    // Using the prefix is safe for COM1..COM9 too.
    std::string device = port_name;
#if defined(_WIN32)
    if (device.rfind("\\\\.\\\\", 0) != 0)
        device = "\\\\.\\\\" + device;
#endif

    int result = serial.openDevice(device.c_str(), 921600);

    if (result == 1) {
        std::cout << "Successfully opened " << port_name << std::endl;
        serial.flushReceiver();
        return true;
    }

    // serialib Windows error codes:
    // -1 device not found
    // -2 error while opening the device
    // -3 error while getting port parameters
    // -5 error while writing port parameters
    // -6 error while writing timeout parameters
    switch (result) {
    case -1:
        std::cerr << "Error: Device not found: " << port_name << std::endl;
        break;
    case -2:
        std::cerr << "Error: Error while opening the device";
#if defined(_WIN32)
        std::cerr << " (GetLastError=" << GetLastError() << ")";
#endif
        std::cerr << std::endl;
        break;
    case -3:
        std::cerr << "Error: Error while getting port parameters" << std::endl;
        break;
    case -5:
        std::cerr << "Error: Error while writing port parameters" << std::endl;
        break;
    case -6:
        std::cerr << "Error: Error while writing timeout parameters" << std::endl;
        break;
    default:
        std::cerr << "Error: Unknown error opening port (code=" << result << ")" << std::endl;
        break;
    }
    return false;
}

#if defined(_WIN32)
static std::vector<ComPortInfo> enumerateComPortsWindows()
{
    std::vector<ComPortInfo> out;

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE)
        return out;

    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(devInfo);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i)
    {
        char friendly[512]{};
        DWORD regType = 0;
        DWORD size = 0;
        if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, &regType,
            reinterpret_cast<PBYTE>(friendly), static_cast<DWORD>(sizeof(friendly) - 1), &size))
        {
            continue;
        }

        std::string desc(friendly);
        auto lparen = desc.rfind("(COM");
        auto rparen = (lparen != std::string::npos) ? desc.find(')', lparen) : std::string::npos;
        if (lparen == std::string::npos || rparen == std::string::npos)
            continue;

        std::string port = desc.substr(lparen + 1, rparen - (lparen + 1));
        out.push_back(ComPortInfo{ port, desc });
    }

SetupDiDestroyDeviceInfoList(hDevInfo);

std::sort(out.begin(), out.end(), [](const ComPortInfo& a, const ComPortInfo& b) {
        auto num = [](const std::string& p) -> int {
            if (p.rfind("COM", 0) != 0) return 100000;
            return std::atoi(p.c_str() + 3);
        };
        const int na = num(a.port);
        const int nb = num(b.port);
        if (na != nb) return na < nb;
        return a.port < b.port;
        });

    out.erase(std::unique(out.begin(), out.end(), [](const ComPortInfo& x, const ComPortInfo& y) {
        return x.port == y.port;
        }), out.end());

    return out;
}
#endif

static void comDiscoveryThreadWorker()
{
    while (!g_discovery_stop_requested.load())
    {
        std::vector<ComPortInfo> ports;
#if defined(_WIN32)
        ports = enumerateComPortsWindows();
#endif

        {
            std::lock_guard<std::mutex> lock(g_ports_mutex);
            g_ports = std::move(ports);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void startComPortAutoDiscovery()
{
    bool expected = false;
    if (!g_discovery_running.compare_exchange_strong(expected, true))
        return;

    g_discovery_stop_requested.store(false);
    g_discovery_thread = std::thread(comDiscoveryThreadWorker);
}

void stopComPortAutoDiscovery()
{
    if (!g_discovery_running.load())
        return;

    g_discovery_stop_requested.store(true);
    if (g_discovery_thread.joinable())
        g_discovery_thread.join();
    g_discovery_running.store(false);
}

std::vector<ComPortInfo> getAvailableComPorts()
{
    std::lock_guard<std::mutex> lock(g_ports_mutex);
    return g_ports;
}

std::string getSelectedComPort()
{
    std::lock_guard<std::mutex> lock(g_selected_port_mutex);
    return g_selected_port;
}

void stopRealDataCapture()
{
    g_capture_stop_requested.store(true);
    if (g_capture_thread.joinable())
        g_capture_thread.join();

    g_capture_running.store(false);
    g_capture_stop_requested.store(false);
    closeSerialNoThrow();
}

static void realDataThreadWorker(const std::string& com_port)
{
    if (!openCOMPort(com_port)) {
        std::cerr << "[REAL DATA] Failed to open " << com_port << std::endl;
        return;
    }

    std::cout << "[REAL DATA] Listening on " << com_port << std::endl;

    uint64_t bytes_seen = 0;
    uint64_t telemetry_headers_seen = 0;
    uint64_t telemetry_packets_ok = 0;
    uint64_t telemetry_packets_bad = 0;
    uint64_t nonmatch_bytes = 0;

    auto last_stats = std::chrono::steady_clock::now();
    auto last_coord_log = std::chrono::steady_clock::now();
    TelemetryPacket last_packet{};
    bool has_last_packet = false;

    // SX1280 device sends RajaTelemetryPacket in little-endian order.
    // magic PACKET_MAGIC_RAJA (0x52414A41 'RAJA') appears on the wire as bytes: 41 4A 41 52
    constexpr uint8_t kMagicLE[4] = { 0x41, 0x4A, 0x41, 0x52 };
    uint8_t window[4] = { 0, 0, 0, 0 };
    int windowCount = 0;

    while (!g_capture_stop_requested.load())
    {
      const auto now = std::chrono::steady_clock::now();
        // Read stream byte-by-byte and scan for magic marker.
        uint8_t byte = 0;
        if (serial.readBytes(&byte, 1, 100) <= 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else
        {
            bytes_seen++;

            // shift window
            window[0] = window[1];
            window[1] = window[2];
            window[2] = window[3];
            window[3] = byte;
            if (windowCount < 4) {
                windowCount++;
                continue;
            }

            const bool matched = (window[0] == kMagicLE[0] && window[1] == kMagicLE[1] && window[2] == kMagicLE[2] && window[3] == kMagicLE[3]);
            if (!matched)
            {
                nonmatch_bytes++;

                // keep scanning
            }
            else
            {
                telemetry_headers_seen++;

                // Read the wire packet after the already matched magic marker.
                // RajaTelemetryPacket is packed; layout must match the device sender.
                RajaTelemetryPacket wire{};
                wire.magic = PACKET_MAGIC_RAJA;
                const size_t payloadSize = sizeof(RajaTelemetryPacket) - sizeof(uint32_t);
                uint8_t* dst = reinterpret_cast<uint8_t*>(&wire) + sizeof(uint32_t);
                size_t totalRead = 0;
                while (totalRead < payloadSize && !g_capture_stop_requested.load())
                {
                    const int r = serial.readBytes(reinterpret_cast<char*>(dst + totalRead), static_cast<int>(payloadSize - totalRead), 100);
                    if (r > 0)
                        totalRead += static_cast<size_t>(r);
                    else
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                // Validate CRC-16 over everything except the trailing crc field.
                const bool crc_ok = (totalRead == payloadSize) &&
                    (raja_crc16(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire) - 2) == wire.crc);

                if (crc_ok)
                {
                    // Translate the wire packet into the app's internal TelemetryPacket.
                    TelemetryPacket packet{};
                    packet.MagicMarker  = PACKET_MAGIC_DATA;
                    packet.lat          = wire.lat;
                    packet.lon          = wire.lon;
                    packet.time         = wire.gps_utc_ms;
                    packet.speed        = wire.speed;
                    packet.acceleration = wire.acceleration;
                    packet.gForceX      = wire.gForceX;
                    packet.gForceY      = wire.gForceY;
                    packet.fixtype      = wire.fix_type;
                    packet.ID           = static_cast<int32_t>(wire.device_id);

                    telemetry_packets_ok++;
                   last_packet = packet;
                    has_last_packet = true;

                    {
                        std::lock_guard<std::mutex> lock(g_last_packet_mutex);
                        g_last_packet = packet;
                        g_has_last_packet.store(true, std::memory_order_relaxed);
                    }

                    processIncomingTelemetry(packet);

                    // ✅ 2. Broadcast to network clients (if server is running)
                    // Only broadcast if in server mode (not client mode)
                    if (g_is_server_mode && !g_is_client_mode) {
                        BroadcastTelemetryToClients(packet);
                    }
                }
                else
                {
                    telemetry_packets_bad++;
                    if ((telemetry_packets_bad % 10) == 1)
                    {
                        std::cerr << "[SERIAL] RAJA packet rejected ("
                                  << (totalRead == payloadSize ? "CRC mismatch" : "short read")
                                  << "). expected=" << payloadSize
                                  << " got=" << totalRead
                                  << " | ok=" << telemetry_packets_ok
                                  << " bad=" << telemetry_packets_bad
                                  << std::endl;
                    }

        // Periodic coordinate log (every 5 seconds)
        if (has_last_packet && (now - last_coord_log >= std::chrono::seconds(5)))
        {
            last_coord_log = now;
           // Arduino packs GPS as scaled integers: degrees * 1e7
            const double lat = static_cast<double>(last_packet.lat) / 1e7;
            const double lon = static_cast<double>(last_packet.lon) / 1e7;
            std::cout << "[SERIAL] last GNSS: lat=" << lat
                      << " lon=" << lon
                      << " fix=" << last_packet.fixtype
                      << " speed=" << (static_cast<double>(last_packet.speed) / 100.0)
                      << "km/h"
                      << std::endl;
        }
                }
            }
        }

        // Periodic stats (every 2 seconds)
        if (now - last_stats >= std::chrono::seconds(2))
        {
            last_stats = now;
            std::cout << "[SERIAL] stats: bytes_seen=" << bytes_seen
                      << " telemetry_headers_seen=" << telemetry_headers_seen
                      << " telemetry_packets_ok=" << telemetry_packets_ok
                      << " telemetry_packets_bad=" << telemetry_packets_bad
                      << " nonmatch_bytes=" << nonmatch_bytes
                      << std::endl;
        }
    }

    std::cout << "[REAL DATA] Stopped listening on " << com_port << std::endl;
    closeSerialNoThrow();
}

void startRealDataCapture(const std::string& com_port)
{
    stopRealDataCapture();
    g_capture_running.store(true);
    g_capture_stop_requested.store(false);
    g_capture_thread = std::thread(realDataThreadWorker, com_port);
}

bool selectAndOpenComPort(const std::string& port)
{
    if (port.empty())
        return false;

    {
        std::lock_guard<std::mutex> lock(g_selected_port_mutex);
        g_selected_port = port;
    }

    std::cout << "[SERIAL] Selected COM port: " << port << std::endl;

    // Avoid showing stale PPS from the previous source while the new port is opening.
    telemetryResetPpsCounters();
    telemetryResetPrototypeIdMapping();
    startRealDataCapture(port);
    return true;
}

void testSerial()
{
    if (openCOMPort("COM5"))
    {
        std::cout << "Connection established. Ready to receive data." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        serial.closeDevice();
        std::cout << "Port closed." << std::endl;
    }
}



