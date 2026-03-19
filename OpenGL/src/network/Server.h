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

// ============================================================================
// PACKET MAGIC MARKERS (centralized)
//
// NOTE: These are macros intentionally, to avoid multiple-definition/redefinition
// issues across translation units in this project configuration.
// ============================================================================
#define PACKET_MAGIC_DATA 0x44415441u // 'DATA' TelemetryPacket
#define PACKET_MAGIC_AUTH 0x41555448u // 'AUTH'
#define PACKET_MAGIC_RESP 0x52455350u // 'RESP'
#define PACKET_MAGIC_TRCK 0x5452434Bu // 'TRCK'
#define PACKET_MAGIC_TCHU 0x54434855u // 'TCHU'
#define PACKET_MAGIC_RACE 0x52414345u // 'RACE'
#define PACKET_MAGIC_VSTA 0x56535441u // 'VSTA'

#pragma pack(push, 1)
struct AuthPacket {
   uint32_t magic_marker;  // PACKET_MAGIC_AUTH
	char password[64];
};

struct AuthResponsePacket {
   uint32_t magic_marker;  // PACKET_MAGIC_RESP
	bool is_authenticated;
	int attempts_remaining;
	char message[128];
};

// Track point for network transmission
struct TrackPointPacket {
	float x;
	float y;
	float tangent_x;
	float tangent_y;
};

// Track data header - sent first
struct TrackDataHeader {
  uint32_t magic_marker;   // PACKET_MAGIC_TRCK
	uint32_t point_count;    // Number of track points to follow
	double origin_lat;       // Map origin latitude
	double origin_lon;       // Map origin longitude
	double origin_easting;   // Origin in UTM meters
	double origin_northing;  // Origin in UTM meters
	int origin_zone;         // UTM zone
	char origin_zone_char;   // UTM zone letter
	float start_finish_p1_x; // Start/finish line point 1
	float start_finish_p1_y;
	float start_finish_p2_x; // Start/finish line point 2
	float start_finish_p2_y;
};

// Track chunk packet - contains multiple points
#define MAX_POINTS_PER_CHUNK 100
struct TrackChunkPacket {
  uint32_t magic_marker;   // PACKET_MAGIC_TCHU
	uint32_t chunk_index;    // Which chunk this is
	uint32_t points_in_chunk; // Number of points in this chunk
	TrackPointPacket points[MAX_POINTS_PER_CHUNK];
};

// Race initialization data
struct RaceDataPacket {
  uint32_t magic_marker;   // PACKET_MAGIC_RACE
	bool has_start_finish_line;
	// Add other race settings here if needed
};

// Processed vehicle state packet - used for simulated/server-authoritative vehicles.
// Sends already normalized state so client does not need GPS->UTM conversion or
// local nearest-segment progress reconstruction.
struct VehicleStatePacket {
 uint32_t magic_marker;        // PACKET_MAGIC_VSTA
	int32_t vehicle_id;
	uint32_t server_time_ms;      // Monotonic server time in milliseconds
	float normalized_x;
	float normalized_y;
	float heading;
	float speed_kph;
	float track_progress;         // 0..1 authoritative progress along track
	float current_lap_time;
	float best_lap_time;
	int32_t completed_laps;
	int32_t current_lap_number;
	uint8_t has_started_first_lap;
	uint8_t is_leader;
};
#pragma pack(pop)

int serverWork();

bool isServerRunning();

// True when listen socket is created and server is ready to accept connections.
bool isServerListening();

void ChangeisServerRunning();

void serverStop();

void continueServerRunning();

// Broadcast telemetry packet to all authenticated clients
void BroadcastTelemetryToClients(const TelemetryPacket& packet);

// Broadcast processed vehicle state to all authenticated clients
void BroadcastVehicleStateToClients(const VehicleStatePacket& packet);

// Send track and race data to newly authenticated client
void SendTrackAndRaceData(HSteamNetConnection connection);

// UI/runtime configurable server password (empty => allow all)
void serverSetPassword(const char* password_or_null);
const char* serverGetPassword();

// Best-effort WAN IP discovered via UPnP (empty if unavailable)
const char* serverGetWanIp();
