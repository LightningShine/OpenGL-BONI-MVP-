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

// UI-driven connection parameters
// If host is empty, clientStart falls back to defaults.
void clientSetConnectParams(const char* host, uint16_t port, const char* password_or_null);
void clientGetConnectParams(std::string& out_host, uint16_t& out_port, std::string& out_password);

// Client auth/result state for UI
bool clientIsAuthenticated();
bool clientHadAuthFailure();
void clientClearAuthState();


bool isClientRunning();

void toggleClientRunning();

void clientStop();

void continueClientRunning();