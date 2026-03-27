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

#if defined(_WIN32)
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#pragma comment(lib, "setupapi.lib")
#endif

serialib serial;

static std::atomic<bool> g_capture_running{ false };
static std::atomic<bool> g_capture_stop_requested{ false };
static std::thread g_capture_thread;
static std::mutex g_serial_mutex;

static std::atomic<bool> g_discovery_running{ false };
static std::atomic<bool> g_discovery_stop_requested{ false };
static std::thread g_discovery_thread;

static std::mutex g_ports_mutex;
static std::vector<ComPortInfo> g_ports;
static std::mutex g_selected_port_mutex;
static std::string g_selected_port;

static void closeSerialNoThrow()
{
    std::lock_guard<std::mutex> lock(g_serial_mutex);
    // serialib has no isOpen() in all versions; just attempt close.
    try { serial.closeDevice(); }
    catch (...) {}
}

bool openCOMPort(const std::string& port_name)
{
    std::lock_guard<std::mutex> lock(g_serial_mutex);
    int result = serial.openDevice(port_name.c_str(), 115200);

    if (result == 1) {
        std::cout << "Successfully opened " << port_name << std::endl;
        serial.flushReceiver();
        return true;
    }

    switch (result) {
    case -1: std::cerr << "Error: Device not found: " << port_name << std::endl; break;
    case -2: std::cerr << "Error: Error while setting port parameters." << std::endl; break;
    case -3: std::cerr << "Error: Another program is already using this port!" << std::endl; break;
    default: std::cerr << "Error: Unknown error opening port." << std::endl; break;
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

    // Arduino/ESP sends struct in little-endian order.
    // MagicMarker PacketMagic::DATA (0x44415441 'DATA') appears on the wire as bytes: 41 54 41 44
    constexpr uint8_t kMagicLE[4] = { 0x41, 0x54, 0x41, 0x44 };
    uint8_t window[4] = { 0, 0, 0, 0 };
    int windowCount = 0;

    while (!g_capture_stop_requested.load())
    {
      const auto now = std::chrono::steady_clock::now();
        // Read stream byte-by-byte and scan for magic marker.
        uint8_t byte = 0;
        if (serial.readBytes(&byte, 1) <= 0)
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

                TelemetryPacket packet{};
                packet.MagicMarker = PACKET_MAGIC_DATA;

                const size_t dataSize = sizeof(TelemetryPacket) - sizeof(uint32_t);
                const int payloadRead = serial.readBytes(reinterpret_cast<char*>(&packet) + 4, dataSize);
                if (payloadRead == static_cast<int>(dataSize))
                {
                    telemetry_packets_ok++;
                   last_packet = packet;
                    has_last_packet = true;
                    processIncomingTelemetry(packet);
                }
                else
                {
                    telemetry_packets_bad++;
                    if ((telemetry_packets_bad % 10) == 1)
                    {
                        std::cerr << "[SERIAL] Telemetry payload read failed. expected=" << dataSize
                                  << " got=" << payloadRead
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



