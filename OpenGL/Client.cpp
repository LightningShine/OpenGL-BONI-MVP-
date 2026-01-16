#include "../network/Client.h"

// CLIENT 

HSteamNetConnection g_hConnection;

bool ClientIsRunning_b = false;
bool ClientShouldClose_b = false;

void OnClientConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	switch (pInfo->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connected:
		std::cout << "Connected to Server" << std::endl;
		break;

	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		std::cout << "LOST: Connection lost. Reason: " << pInfo->m_info.m_szEndDebug << std::endl;
		SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
		g_hConnection = k_HSteamNetConnection_Invalid;
	}
}

void ConnectToServer(const SteamNetworkingIPAddr& serverAddr)
{
	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)OnClientConnectionStatusChanged);
	g_hConnection = SteamNetworkingSockets()->ConnectByIPAddress(serverAddr, 1, &opt);

	if (g_hConnection == k_HSteamNetConnection_Invalid)
	{
		std::cerr << "Failed to initiate connection to server." << std::endl;
		return;
	}
}


void SendClientMessage(const char* text)
{
	SteamNetworkingSockets()->SendMessageToConnection(
		g_hConnection,
		text,
		(uint32)strlen(text),
		k_nSteamNetworkingSend_Reliable,
		nullptr
	);
}

void ListenMessagesFromServer()
{
	ISteamNetworkingMessage* pIncomingMsg[16];
	int numMsgs = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_hConnection, pIncomingMsg, 16);

	if (numMsgs < 0) {
		std::cerr << "Error receiving messages from server." << std::endl;
	}

	for (int i = 0; i < numMsgs; ++i)
	{
		TelemetryPacket* pData = (TelemetryPacket*)pIncomingMsg[i]->m_pData;
		std::cout << "Received Telemetry Packet from Server:" << std::endl;
		std::cout << "  ID: " << pData->ID << std::endl
			<< "  Latitude: " << pData->lat / 1e7 << std::endl
			<< "  Longitude: " << pData->lon / 1e7 << std::endl
			<< "  Time: " << pData->time << " ms" << std::endl
			<< "  Speed: " << pData->speed / 100.0 << " km/h" << std::endl
			<< "  G-Force X: " << pData->gForceX / 100.0 << " G" << std::endl
			<< "  G-Force Y: " << pData->gForceY / 100.0 << " G" << std::endl
			<< "  Acceleration: " << (float)pData->acceleration / 100.0f << " m/s^2" << std::endl
			<< "  Fix Type: " << pData->fixtype << std::endl;


		//std::string msgText((const char*)pIncomingMsg[i]->m_pData, pIncomingMsg[i]->m_cbSize);
		//std::cout << "SERVER SAYS: " << msgText << std::endl;


		pIncomingMsg[i]->Release();
	}
}


bool ClientRunningStatus()
{
	return ClientIsRunning_b;

}

void ChangeClientRunningStatus()
{
	ClientIsRunning_b = !ClientIsRunning_b;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void ClientStop()
{
	ClientShouldClose_b = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void ContinueClientRunning()
{
	ClientShouldClose_b = false;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int ClientStart()
{
	std::cout << "Starting GNS Client..." << std::endl;
	SteamDatagramErrMsg errMsg;
	if (!GameNetworkingSockets_Init(nullptr, errMsg)) return 1;

	SteamNetworkingIPAddr serverAddr;
	serverAddr.Clear();
	serverAddr.ParseString("127.0.0.1:777");
	//serverAddr.ParseString("136.169.29.56:777");

	ConnectToServer(serverAddr);
	std::string message;
	int counter = 400;
	while (!ClientShouldClose_b)
	{

		SteamNetworkingSockets()->RunCallbacks();

		ListenMessagesFromServer();

		if (counter >= 600)
		{
			SteamNetConnectionInfo_t info;
			SteamNetworkingSockets()->GetConnectionInfo(g_hConnection, &info);

			if (info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
				std::cout << "Sending message to server..." << std::endl;
				SendClientMessage("Hello Server");
			}
			else {
				std::cout << "Still waiting for connection... (State: " << info.m_eState << ")" << std::endl;
				break;
			}
			counter = 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		++counter;
	}

	std::cout << "Shutting down GNS Client..." << std::endl;
	GameNetworkingSockets_Kill();

	return 0;
}

