#pragma once
#include <iostream>
#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
#include "../network/Server.h"
#include <thread>


// CLIENT 
int ClientStart();


bool ClientRunningStatus();

void ChangeClientRunningStatus();

void ClientStop();

void ContinueClientRunning();