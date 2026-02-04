#pragma once
#include "../network/Server.h"
#include <serialib/serialib.h>
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"
#include <thread>
#include <chrono>
#include <mutex>
#include <glm/glm.hpp>


enum class PacketType : uint32_t {
	HANDSHAKE = 0x4E45570A,  // Device try to connect
	TELEMETRY = 0x44415441,  // Telemetry Data Packet
	DEV_TEST  = 0x54455354,  // Test Packet
	ALERT     = 0x4552520A   // Error in Packet
};

void testSerial();
bool openCOMPort(const std::string& port_name);
void readFromCOM();

// Simulate vehicle movement along pre-interpolated track
void simulateVehicleMovement(int vehicle_id, 
                             const std::vector<SplinePoint>& smooth_track_points);
bool openCOMPort(const std::string& port_name);