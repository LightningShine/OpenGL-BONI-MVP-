#pragma once
#include "../network/Server.h"
#include "../serialib/serialib.h"


enum class PacketType : uint32_t {
	HANDSHAKE = 0x4E45570A,  // Device try to connect
	TELEMETRY = 0x44415441,  // Telemetry Data Packet
	DEV_TEST  = 0x54455354,  // Test Packet
	ALERT     = 0x4552520A   // Error in Packet
};

void Test_Serial();
bool OpenCOMPort(const std::string& portName);