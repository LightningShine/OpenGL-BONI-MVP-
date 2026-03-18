#include "../network/ESP32_Code.h"
#include "SimulationServer.h"
#include <iostream>
#include <thread>
#include <chrono>

serialib serial;

bool openCOMPort(const std::string& port_name)
{
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

    // Arduino/ESP sends struct in little-endian order.
    // MagicMarker PacketMagic::DATA (0x44415441 'DATA') appears on the wire as bytes: 41 54 41 44
    constexpr uint8_t kMagicLE[4] = { 0x41, 0x54, 0x41, 0x44 };
    uint8_t window[4] = { 0, 0, 0, 0 };
    int windowCount = 0;

    while (true)
    {
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
                }
            }
        }

        // Periodic stats (every 2 seconds)
        const auto now = std::chrono::steady_clock::now();
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
}

void startRealDataCapture(const std::string& com_port)
{
    std::thread real_thread(realDataThreadWorker, com_port);
    real_thread.detach();
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



