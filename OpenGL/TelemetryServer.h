#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

// Telemetry constants
#define TELEMETRY_DEFAULT_PORT      8080
#define TELEMETRY_BUFFER_SIZE       1024
#define TELEMETRY_ACC_UNAVAILABLE   (-911)
#define TELEMETRY_LAT_LON_SCALE     1e-7
#define TELEMETRY_COURSE_SCALE      100
#define TELEMETRY_SPEED_SCALE       100     // cm/s to m/s
#define TELEMETRY_ALT_SCALE         1000    // mm to m

// GPS fix types
enum class GpsFixType : uint8_t
{
    None = 0,
    Fix2D = 2,
    Fix3D = 3,
    RTK_Fixed = 4,
    RTK_Float = 5
};

// Raw telemetry packet from device (matches photo structure)
struct TelemetryPacket
{
    std::string deviceId;
    int32_t lat;            // degrees * 1e7
    int32_t lon;            // degrees * 1e7
    int32_t alt;            // mm
    uint32_t time;          // milliseconds UTC
    uint16_t speed;         // cm/s
    uint16_t course;        // degrees * 100
    uint8_t fixType;        // 0=none, 4=RTK_FIXED, etc
    uint8_t sats;           // # satellites
    int32_t acc;            // acceleration (-911 = unavailable)
};

// Parsed telemetry with real values
struct ParsedTelemetry
{
    std::string deviceId;
    double latitude;        // decimal degrees
    double longitude;       // decimal degrees
    double altitude;        // meters
    uint32_t timestamp;     // milliseconds UTC
    double speed;           // m/s
    double course;          // degrees
    GpsFixType fixType;
    uint8_t satellites;
    double acceleration;    // m/s^2
    bool accelerationValid;
};

class TelemetryServer
{
public:
    using TelemetryCallback = std::function<void(const ParsedTelemetry&)>;

    TelemetryServer();
    ~TelemetryServer();

    // Start server on specified port
    bool Start(uint16_t port = TELEMETRY_DEFAULT_PORT);
    
    // Stop server
    void Stop();
    
    // Check if server is running
    bool IsRunning() const;

    // Set callback for received telemetry
    void SetCallback(TelemetryCallback callback);

    // Process JSON string manually (for testing or serial input)
    bool ProcessJson(const std::string& jsonStr);

private:
    void ServerLoop();
    bool ParseTelemetryJson(const std::string& jsonStr, TelemetryPacket& packet);
    ParsedTelemetry ConvertToReal(const TelemetryPacket& packet);

    std::thread m_serverThread;
    std::atomic<bool> m_running;
    std::atomic<uint16_t> m_port;
    TelemetryCallback m_callback;
    mutable std::mutex m_callbackMutex;
};
