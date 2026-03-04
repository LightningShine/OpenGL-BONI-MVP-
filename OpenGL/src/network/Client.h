#pragma once
#include <iostream>

// GameNetworkingSockets doesn't build on ARM64 Windows
#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
#define NETWORKING_ENABLED 1
#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
#else
#define NETWORKING_ENABLED 0
#endif

#include "../network/Server.h"
#include <thread>
#include <regex>
#include "../Config.h"
#include <glm/glm.hpp>     // ✅ For glm::vec2
#include <mutex>           // ✅ For std::mutex


// CLIENT 
int clientStart(const std::vector<glm::vec2>& track_points, std::mutex& points_mutex);


bool isClientRunning();

void toggleClientRunning();

void clientStop();

void continueClientRunning();