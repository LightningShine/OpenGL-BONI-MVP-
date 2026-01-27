#pragma once
#include <iostream>
#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
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