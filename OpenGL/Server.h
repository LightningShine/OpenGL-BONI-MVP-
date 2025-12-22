#pragma once

#include <cstdint>
#include <string>

// GPS fix types
enum class GpsFixType : uint8_t
{
    None = 0,
    Fix2D = 2,
    Fix3D = 3,
    RTK_Fixed = 4,
    RTK_Float = 5
};

// Parsed telemetry with real values
struct ParsedTelemetry
{
    std::string deviceId;
    double latitude;        // decimal degrees
    double longitude;       // decimal degrees
    double altitude;        // meters
    uint64_t timestamp;     // milliseconds UTC
    double speed;           // m/s
    double course;          // degrees
    GpsFixType fixType;
    uint8_t satellites;
    double acceleration;    // m/s^2
    bool accelerationValid;
};

class VenchileManager;

bool StartGlobalHttpServer(VenchileManager* manager, uint16_t port = 8080);
void StopGlobalHttpServer();
bool IsGlobalHttpServerRunning();
