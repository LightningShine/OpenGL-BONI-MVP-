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


// CLIENT 
int clientStart();


bool isClientRunning();

void toggleClientRunning();

void clientStop();

void continueClientRunning();