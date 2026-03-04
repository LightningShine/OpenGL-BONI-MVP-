#pragma once

#include <cstdint>
#include <iostream>

// GameNetworkingSockets doesn't build on ARM64 Windows
#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
#define NETWORKING_ENABLED 1
#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
#include "miniupnpc/miniupnpc.h"
#include "miniupnpc/upnpcommands.h"
#else
#define NETWORKING_ENABLED 0
// Stub types for ARM64 compilation
typedef void* HSteamNetConnection;
typedef void* ISteamNetworkingSockets;
#endif

#include "../Config.h"
#include "windows.h"
#include <vector>
#include <thread>
#include <random>
#include <algorithm>
#include <map>
#include <mutex>           // ✅ For std::mutex
#include <glm/glm.hpp>     // ✅ For glm::vec2


#pragma pack(push, 1)
struct TelemetryPacket 
{
	uint32_t MagicMarker;	// 'DATA' 0x44415441
	int32_t lat;			// latitude degree * 1e7
	int32_t lon;			// longitude degree * 1e7
	uint32_t time;			// time in milliseconds
	uint32_t speed;			// speed in km/h * 100
	uint32_t acceleration;	// acceleration * 100
	uint16_t gForceX;		// g-force X * 100
	uint16_t gForceY;		// g-force Y * 100
	int16_t fixtype;		// 0=none, 4=RTK_FIXED, etc.
	int32_t ID;			    // vehicle ID
};
#pragma pack(pop)

#pragma pack(push, 1)
struct AuthPacket {
	uint32_t magic_marker;  // 0x41555448 ('AUTH')
	char password[64];
};

struct AuthResponsePacket {
	uint32_t magic_marker;  // 0x52455350 ('RESP')
	bool is_authenticated;
	int attempts_remaining;
	char message[128];
};

// ============================================================================
// ✅ MAP SYNCHRONIZATION PACKETS
// ============================================================================

struct MapRequestPacket {
	uint32_t magic_marker = PacketMagic::MAP_REQUEST;  // 'MAPR'
	uint32_t client_timestamp;  // For detecting retries
};

struct MapDataPacket {
	uint32_t magic_marker = PacketMagic::MAP_DATA;  // 'MAPD'

	// Map origin data (from g_map_origin)
	double origin_lat_dd;
	double origin_lon_dd;
	double origin_meters_easting;
	double origin_meters_northing;
	int origin_zone_int;
	char origin_zone_char;
	double map_size;

	uint32_t total_points;      // Total number of track points
	uint32_t total_packets;     // How many MapPointsPacket to expect
	uint32_t server_timestamp;  // For packet loss detection
};

struct MapPointsPacket {
	uint32_t magic_marker = PacketMagic::MAP_POINTS;  // 'MAPP'
	uint32_t sequence_number;   // Packet index (0, 1, 2...)
	uint32_t total_packets;     // Total number of packets in sequence
	uint32_t num_points;        // Number of points IN THIS packet
	uint32_t server_timestamp;  // For duplicate detection

	// Max ~80 points per packet (UDP limit ~1200 bytes)
	struct TrackPoint {
		double lat_dd;
		double lon_dd;
	} points[80];
};
#pragma pack(pop)

int serverWork(const std::vector<glm::vec2>& track_points, std::mutex& points_mutex);

bool isServerRunning();

void ChangeisServerRunning();

void serverStop();

void continueServerRunning();

// Broadcast telemetry packet to all authenticated clients
void BroadcastTelemetryToClients(const TelemetryPacket& packet);

// ✅ Mode flags (server and client are mutually exclusive)
extern std::atomic<bool> g_is_server_mode;
extern std::atomic<bool> g_is_client_mode;
