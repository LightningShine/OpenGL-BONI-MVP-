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
<<<<<<< HEAD
#else
#define NETWORKING_ENABLED 0
// Stub types for ARM64 compilation
typedef void* HSteamNetConnection;
typedef void* ISteamNetworkingSockets;
#endif

=======
#include "../Config.h"
#include "windows.h"
>>>>>>> origin/dev
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
#pragma pack(pop)

int serverWork();

bool isServerRunning();

void ChangeisServerRunning();

void serverStop();

void continueServerRunning();
