#include "../network/Client.h"
#include "../Config.h"
#include <cstring>
#include <windows.h>

#if NETWORKING_ENABLED

// CLIENT 

HSteamNetConnection g_connection_handle;

bool g_is_client_running = false;
bool g_should_close_client = false;
bool ErrnumMsg_b = false;

void onClientConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
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
		g_connection_handle = k_HSteamNetConnection_Invalid;
	}
}

// Send authentication packet to server
static bool sendAuthPacket(HSteamNetConnection connection, const std::string& password)
{
	// Guard clause
	if (connection == k_HSteamNetConnection_Invalid) {
		std::cerr << "Cannot send auth - invalid connection" << std::endl;
		return false;
	}
	
	AuthPacket auth_packet;
	auth_packet.magic_marker = PacketMagic::AUTH;
	strncpy_s(auth_packet.password, password.c_str(), sizeof(auth_packet.password) - 1);
	auth_packet.password[sizeof(auth_packet.password) - 1] = '\0';  // Ensure null termination
	
	EResult result = SteamNetworkingSockets()->SendMessageToConnection(
		connection,
		&auth_packet,
		sizeof(auth_packet),
		k_nSteamNetworkingSend_Reliable,
		nullptr
	);
	
	if (result == k_EResultOK) {
		std::cout << "? Authentication packet sent" << std::endl;
		return true;
	}
	
	std::cerr << "? Failed to send authentication packet" << std::endl;
	return false;
}

void connectToServer(const SteamNetworkingIPAddr& server_address)
{
	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)onClientConnectionStatusChanged);
	g_connection_handle = SteamNetworkingSockets()->ConnectByIPAddress(server_address, 1, &opt);

	if (g_connection_handle == k_HSteamNetConnection_Invalid)
	{
		std::cerr << "Failed to initiate connection to server." << std::endl;
		return;
	}
}


void sendClientMessage(const char* text)
{
	SteamNetworkingSockets()->SendMessageToConnection(
		g_connection_handle,
		text,
		(uint32)strlen(text),
		k_nSteamNetworkingSend_Reliable,
		nullptr
	);
}

void listenMessagesFromServer()
{
	ISteamNetworkingMessage* pIncomingMsg[16];
	int numMsgs = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_connection_handle, pIncomingMsg, 16);

	/*if (numMsgs < 0 && !ErrnumMsg_b)
	{
		ErrnumMsg_b = true;
		std::cerr << "Error receiving messages from server." << std::endl;
	}*/

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

		pIncomingMsg[i]->Release();
		ErrnumMsg_b = false;
	}
}


bool isClientRunning()
{
	return g_is_client_running;

}

void toggleClientRunning()
{
	g_is_client_running = !g_is_client_running;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void clientStop()
{
	g_should_close_client = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void continueClientRunning()
{
	g_should_close_client = false;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int clientStart()
{
	std::cout << "Starting GNS Client..." << std::endl;
	SteamDatagramErrMsg error_message;
	std::string server_name;
	std::regex ip_port_pattern(R"(^(\d{1,3}\.){3}\d{1,3}:\d{1,5}$)");
	if (!GameNetworkingSockets_Init(nullptr, error_message)) return 1;

	SteamNetworkingIPAddr server_address;
	server_address.Clear();
	std::getline(std::cin, server_name);
	if (server_name == "d" || server_name == " " || server_name == "\n")
	{
		//std::string ip = NetworkConstants::DEFAULT_SERVER_IP;
		std::string default_address = std::string(NetworkConstants::DEFAULT_SERVER_IP) + ":" + std::to_string(NetworkConstants::DEFAULT_SERVER_PORT);
		server_address.ParseString(default_address.c_str());
		std::cout << "Using default server address: " << default_address << std::endl;
	}
	else if(std::regex_match(server_name, ip_port_pattern))
	{
		server_address.ParseString(server_name.c_str());
		std::cout << "Using server address: " << server_name << std::endl;
	}
	else if (server_name == "l")
	{
		std::string local_server_ip = std::string(NetworkConstants::DEFAULT_LOCAL_SERVER_IP) + ":" + std::to_string(NetworkConstants::DEFAULT_SERVER_PORT);
		server_address.ParseString(local_server_ip.c_str());
	}
	else
	{
		std::cerr << "Invalid server address format." << std::endl;
		std::cout << "Shutting down GNS Client..." << std::endl;
		GameNetworkingSockets_Kill();
		return 1; 
	}



	connectToServer(server_address);
	
	// Wait for connection to establish
	std::cout << "Waiting for connection..." << std::endl;
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	
	// Authentication loop with retry attempts
	bool is_authenticated = false;
	int auth_attempts = 0;
	
	while (!is_authenticated && auth_attempts < NetworkConstants::MAX_AUTH_ATTEMPTS && !g_should_close_client)
	{
		std::string password;
		std::cout << "Enter server password: ";
		std::getline(std::cin, password);
		
		if (!sendAuthPacket(g_connection_handle, password)) {
			std::cerr << "Failed to send authentication packet" << std::endl;
			break;
		}
		
		auth_attempts++;
		
		// Wait for auth response from server
		std::cout << "Authenticating..." << std::endl;
		int response_timeout = 0;
		bool received_response = false;
		
		while (response_timeout < 50 && !received_response && !g_should_close_client) {
			SteamNetworkingSockets()->RunCallbacks();
			
			ISteamNetworkingMessage* messages[16];
			int msg_count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_connection_handle, messages, 16);
			
			for (int i = 0; i < msg_count; i++) {
				if (messages[i]->m_cbSize == sizeof(AuthResponsePacket)) {
					AuthResponsePacket* response = (AuthResponsePacket*)messages[i]->m_pData;
					if (response->magic_marker == PacketMagic::RESP) {
						received_response = true;
						
						if (response->is_authenticated) {
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_GREEN);
							std::cout << "Authentication successful!" << std::endl;
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);
							is_authenticated = true;
						} else {
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
							std::cout << "Authentication failed: " << response->message << std::endl;
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);
							
							if (response->attempts_remaining <= 0) {
								std::cerr << "Max attempts exceeded - connection will close" << std::endl;
								g_should_close_client = true;
							}
						}
					}
				}
				messages[i]->Release();
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			response_timeout++;
		}
		
		if (!received_response && !is_authenticated) {
			std::cout << "No response from server - retrying..." << std::endl;
		}
	}
	
	if (!is_authenticated) {
		std::cerr << "Authentication failed. Closing connection." << std::endl;
		GameNetworkingSockets_Kill();
		return 1;
	}
	
	std::string message;
	int counter = 400;
	while (!g_should_close_client)
	{

		SteamNetworkingSockets()->RunCallbacks();

		listenMessagesFromServer();

		if (counter >= 600)
		{
			SteamNetConnectionInfo_t info;
			SteamNetworkingSockets()->GetConnectionInfo(g_connection_handle, &info);

			if (info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
				std::cout << "Sending message to server..." << std::endl;
				sendClientMessage("Hello Server");
			}
			else {
				std::cout << "Still waiting for connection... (State: " << info.m_eState << ")" << std::endl;
				
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

#else

// ARM64 stub implementations
bool g_is_client_running = false;

int clientStart() { return 0; }
void clientStop() {}
void toggleClientRunning() {}
bool isClientRunning() { return false; }
void continueClientRunning() {}

#endif // NETWORKING_ENABLED

