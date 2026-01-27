#include "../network/Server.h"
#include "../Config.h"
#include <cstring>
#include <windows.h>

// Console colors (anonymous namespace for internal use)
namespace {
    constexpr WORD CONSOLE_DEFAULT = 7;
    constexpr WORD CONSOLE_COLOR_GREEN = 10;
    constexpr WORD CONSOLE_COLOR_RED = 12;
    constexpr WORD CONSOLE_COLOR_YELLOW = 14;
}





// SERVER 

HSteamListenSocket g_hListenSocket;
HSteamNetPollGroup g_hPollGroup;
std::vector<HSteamNetConnection> g_hConnections;
static std::map<HSteamNetConnection, bool> g_authenticated_connections;
static std::map<HSteamNetConnection, int> g_auth_attempts;

bool g_is_server_running = false;
bool ServerNeedStop_b = false;


void DebugOutput(ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg)
{
	std::cerr << "GNS Debug Output [" << eType << "]: " << pszMsg << std::endl;
}

void OnConnectionStatusChange(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	HSteamNetConnection hConn = pInfo->m_hConn;
	ESteamNetworkingConnectionState eState = pInfo->m_info.m_eState;

	switch (eState)
	{
	case k_ESteamNetworkingConnectionState_None:
		break;
	case k_ESteamNetworkingConnectionState_Connecting:
		std::cout << "Client attempting to connect: " << hConn << std::endl;
		
		// Accept connection immediately to receive auth packet
		if (SteamNetworkingSockets()->AcceptConnection(hConn) == k_EResultOK) {
			std::cout << "Connection accepted, waiting for authentication..." << std::endl;
			
			// Add to poll group to receive messages
			SteamNetworkingSockets()->SetConnectionPollGroup(hConn, g_hPollGroup);
			
			// Mark as NOT authenticated yet
			g_authenticated_connections[hConn] = false;
		} else {
			std::cerr << "Failed to accept connection" << std::endl;
		}
		break;
		break;
	case k_ESteamNetworkingConnectionState_FindingRoute:
		break;
	case k_ESteamNetworkingConnectionState_Connected:
		std::cout << "Client connected. Waiting for auth..." << std::endl;
		g_hConnections.push_back(hConn);
		g_authenticated_connections[hConn] = false;  
		break;
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
		std::cout << "Connection closed by peer: " << hConn << std::endl;
		g_hConnections.erase(std::remove(g_hConnections.begin(), g_hConnections.end(), hConn), g_hConnections.end());
		g_authenticated_connections.erase(hConn);
		g_auth_attempts.erase(hConn);
		SteamNetworkingSockets()->CloseConnection(hConn, 0, "Closed by peer", false);
		break;
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		std::cout << "Connection problem detected: " << hConn << std::endl;
		g_hConnections.erase(std::remove(g_hConnections.begin(), g_hConnections.end(), hConn), g_hConnections.end());
		g_authenticated_connections.erase(hConn);
		g_auth_attempts.erase(hConn);
		SteamNetworkingSockets()->CloseConnection(hConn, 0, "Closed due to local problem", false);
		break;
	case k_ESteamNetworkingConnectionState_FinWait:
		break;
	case k_ESteamNetworkingConnectionState_Linger:
		break;
	case k_ESteamNetworkingConnectionState_Dead:
		std::cout << "Connection dead: " << hConn << std::endl;
		break;
	case k_ESteamNetworkingConnectionState__Force32Bit:
		break;
	default:
		break;
	}
}

class MyServer {
public:
	static MyServer* s_pInst;
	void OnConnChange(SteamNetConnectionStatusChangedCallback_t* pInfo)
	{
		HSteamNetConnection hConn = pInfo->m_hConn;
		ESteamNetworkingConnectionState eState = pInfo->m_info.m_eState;

		switch (eState)
		{
		case k_ESteamNetworkingConnectionState_None:
			break;
	case k_ESteamNetworkingConnectionState_Connecting:
		std::cout << "Client attempting to connect: " << hConn << std::endl;
		
		// Accept connection immediately to receive auth packet
		if (SteamNetworkingSockets()->AcceptConnection(hConn) == k_EResultOK) {
			std::cout << "Connection accepted, waiting for authentication..." << std::endl;
			
			// Add to poll group to receive messages
			SteamNetworkingSockets()->SetConnectionPollGroup(hConn, g_hPollGroup);
			
			// Mark as NOT authenticated yet
			g_authenticated_connections[hConn] = false;
		} else {
			std::cerr << "Failed to accept connection" << std::endl;
		}
		break;
			break;
		case k_ESteamNetworkingConnectionState_Connected:
			std::cout << "Connection established: " << hConn << std::endl;
			break;
		case k_ESteamNetworkingConnectionState_ClosedByPeer:
			break;
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
			std::cout << "Connection problem detected: " << hConn << std::endl;
			SteamNetworkingSockets()->CloseConnection(hConn, 0, "Closing due to local problem", false);
			break;
		case k_ESteamNetworkingConnectionState_FinWait:
			break;
		case k_ESteamNetworkingConnectionState_Linger:
			break;
		case k_ESteamNetworkingConnectionState_Dead:
			break;
		case k_ESteamNetworkingConnectionState__Force32Bit:
			break;
		default:
			break;
		}
	}
};


void StartServer(uint16 nPort)
{
	SteamNetworkingIPAddr serverAddr;
	serverAddr.Clear();
	serverAddr.m_port = nPort;

	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)OnConnectionStatusChange);
	g_hListenSocket = SteamNetworkingSockets()->CreateListenSocketIP(serverAddr, 1, &opt);

	if (g_hListenSocket == k_HSteamListenSocket_Invalid)
	{
		std::cerr << "Failed to create listen socket on port " << nPort << std::endl;
		return;
	}

	g_hPollGroup = SteamNetworkingSockets()->CreatePollGroup();
	if (g_hPollGroup == k_HSteamNetPollGroup_Invalid)
	{
		std::cerr << "Failed to create poll group" << std::endl;
		SteamNetworkingSockets()->CloseListenSocket(g_hListenSocket);
		return;
	}

	std::cout << "Server started, listening on port " << nPort << std::endl;
}

void SendToAll(const void* pData, uint32 nSize)
{
	for (auto& hConn : g_hConnections)
	{
		// Only send to authenticated clients
		if (g_authenticated_connections[hConn]) {
			SteamNetworkingSockets()->SendMessageToConnection(hConn, pData, nSize, k_nSteamNetworkingSend_Unreliable, nullptr);
		}
	}
}

// Authenticate connection with password
static bool authenticateConnection(HSteamNetConnection connection, const char* password)
{
	// Guard clause - check if already authenticated
	if (g_authenticated_connections[connection]) {
		return true;
	}
	
	// Initialize attempts counter if needed
	if (g_auth_attempts.find(connection) == g_auth_attempts.end()) {
		g_auth_attempts[connection] = 0;
	}
	
	// Increment attempt counter
	g_auth_attempts[connection]++;
	int attempts_used = g_auth_attempts[connection];
	int attempts_remaining = NetworkConstants::MAX_AUTH_ATTEMPTS - attempts_used;
	
	AuthResponsePacket response;
	response.magic_marker = PacketMagic::RESP;
	
	// Verify password
	if (strcmp(password, NetworkConstants::SERVER_PASSWORD) == 0)
	{
		// Authentication successful
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), CONSOLE_COLOR_GREEN);
		std::cout << "Client " << connection << " authenticated successfully" << std::endl;
		std::cout.flush();
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), CONSOLE_DEFAULT);
		
		g_authenticated_connections[connection] = true;
		response.is_authenticated = true;
		response.attempts_remaining = 0;
		strncpy_s(response.message, "Authentication successful", sizeof(response.message) - 1);
		
		// Send success response
		SteamNetworkingSockets()->SendMessageToConnection(
			connection,
			&response,
			sizeof(response),
			k_nSteamNetworkingSend_Reliable,
			nullptr
		);
		
		return true;
	}
	
	// Authentication failed
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), CONSOLE_COLOR_RED);
	std::cout << "Authentication failed for client " << connection 
	          << " (attempt " << attempts_used << "/" << NetworkConstants::MAX_AUTH_ATTEMPTS << ")" << std::endl;
	std::cout.flush();
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), CONSOLE_DEFAULT);
	
	response.is_authenticated = false;
	response.attempts_remaining = attempts_remaining;
	
	// Check if max attempts exceeded
	if (attempts_remaining <= 0) {
		strncpy_s(response.message, "Max attempts exceeded - disconnecting", sizeof(response.message) - 1);
		
		// Send response before closing
		SteamNetworkingSockets()->SendMessageToConnection(
			connection,
			&response,
			sizeof(response),
			k_nSteamNetworkingSend_Reliable,
			nullptr
		);
		
		// Give time to send before closing
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		SteamNetworkingSockets()->CloseConnection(connection, 0, "Max authentication attempts exceeded", false);
		
		std::cout << "Client " << connection << " disconnected - max attempts exceeded" << std::endl;
	} else {
		// Still has attempts remaining
		snprintf(response.message, sizeof(response.message), 
		         "Invalid password - %d attempts remaining", attempts_remaining);
		
		// Send response
		SteamNetworkingSockets()->SendMessageToConnection(
			connection,
			&response,
			sizeof(response),
			k_nSteamNetworkingSend_Reliable,
			nullptr
		);
	}
	
	return false;
}

void FrameUpdate()
{
	SteamNetworkingSockets()->RunCallbacks();

	ISteamNetworkingMessage* incoming_messages[16];
	int message_count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(g_hPollGroup, incoming_messages, 16);

	for (int i = 0; i < message_count; ++i)
	{
		auto* message = incoming_messages[i];
		HSteamNetConnection connection = message->GetConnection();
		
		// Check for auth packet (guard clause)
		if (message->m_cbSize == sizeof(AuthPacket)) {
			AuthPacket* auth_packet = (AuthPacket*)message->m_pData;
			if (auth_packet->magic_marker == PacketMagic::AUTH) {
				authenticateConnection(connection, auth_packet->password);
				message->Release();
				continue;
			}
		}
		
		// Reject if not authenticated (guard clause)
		if (!g_authenticated_connections[connection]) {
			std::cout << "Rejected message from unauthenticated client " << connection << std::endl;
			message->Release();
			continue;
		}
		
		// Process authenticated message
		std::string message_text((const char*)message->m_pData, message->m_cbSize);
		std::cout << "Received from client " << connection << ": " << message_text << std::endl;
		
		message->Release();
	}
}

void RandomTelemetryData(TelemetryPacket& packet);

bool isServerRunning()
{
	return g_is_server_running;
}

void ChangeisServerRunning()
{
	g_is_server_running = !g_is_server_running;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void serverStop()
{
	ServerNeedStop_b = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void continueServerRunning()
{
	ServerNeedStop_b = false;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	
}

void ManagePort(bool open);

// SERVER MAIN FUNCTION ////////////////////////////////////

int serverWork()
{

	std::cout << "Starting GNS Server..." << std::endl;
	TelemetryPacket myPacket;
	SteamDatagramErrMsg errMsg;


	if (!GameNetworkingSockets_Init(nullptr, errMsg))
	{
		std::cerr << "Failed to initialize GNS: " << errMsg << std::endl;
		return 1;
	}

	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, DebugOutput);

	std::cout << "GNS initialized successfully." << std::endl;
	ManagePort(true);
	StartServer(NetworkConstants::DEFAULT_SERVER_PORT);

	int counter = 0;
	while (!ServerNeedStop_b)
	{
		if (counter >= 120)
		{
			// Check if there are any authenticated clients before sending
			bool has_authenticated_clients = false;
			for (const auto& pair : g_authenticated_connections) {
				if (pair.second) {
					has_authenticated_clients = true;
					break;
				}
			}
			
			if (has_authenticated_clients) {
				RandomTelemetryData(myPacket);
				SendToAll(&myPacket, sizeof(TelemetryPacket));
			}
			counter = 0;
		}
		FrameUpdate();
		++counter;

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	ManagePort(false);
	GameNetworkingSockets_Kill();
	std::cout << "Server Die" << std::endl;

	return 0;

}

/////////////////////////////////////////////////////////

void RandomTelemetryData(TelemetryPacket& packet)
{
	static std::default_random_engine generator;
	std::uniform_int_distribution<int32_t> latDist(-900000000, 900000000);   // latitude degree * 1e7
	std::uniform_int_distribution<int32_t> lonDist(-1800000000, 1800000000); // longitude degree * 1e7
	std::uniform_int_distribution<uint32_t> timeDist(0, 86400000);			 // milliseconds in a day
	std::uniform_int_distribution<uint32_t> speedDist(0, 300000);		     // 0 to 300 km/h (speed implies magnitude usually, but int is fine if strictly int32)
	std::uniform_int_distribution<int32_t> gForceDist(-500, 500);			 // -5g to 5g. User had uint32_t with negative range?
	std::uniform_int_distribution<int32_t> accelDist(-10000, 10000);		 // -10 to 10 m/s�. User had uint32_t with negative range?
	std::uniform_int_distribution<int16_t> fixTypeDist(0, 5);		 // fix types. uniform_int_distribution does not support char/uint8_t
	std::uniform_int_distribution<int32_t> idDist(1, 1000000);				 // vehicle IDs

	packet.lat = latDist(generator);
	packet.lon = lonDist(generator);
	packet.time = timeDist(generator);
	packet.speed = speedDist(generator);
	packet.gForceX = (uint16_t)gForceDist(generator);
	packet.gForceY = (uint16_t)gForceDist(generator);
	packet.acceleration = (uint32_t)accelDist(generator);
	packet.fixtype = fixTypeDist(generator);
	packet.ID = idDist(generator);




}

void ManagePort(bool open)
{
	int error = 0;
	struct UPNPDev* devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &error);
	struct UPNPUrls urls;
	struct IGDdatas data;
	char lanaddr[64];
	char wanaddr[64];
	int r = UPNP_AddPortMapping(NULL, NULL,
		"777", "777", lanaddr, NULL, "UDP", NULL, "86400");
	int status = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr), wanaddr, sizeof(wanaddr));

	if (UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr), wanaddr, sizeof(wanaddr)))
	{
		if (open)
		{
			UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
				"777", "777", lanaddr, "MyTelemetryServer", "UDP", NULL, "86400");
			
			if (r != UPNPCOMMAND_SUCCESS)
			{
				std::cout << "AddPortMapping failed with code: " << r << " GetValidID status: " << status << std::endl;
				std::cout << "LAN IP: " << lanaddr << std::endl;
				std::cout << "WAN IP: " << wanaddr << std::endl;
			}
			else
			{
				std::cout << "UPNP: Port 777 opened automaticaly!" << std::endl;
			}
		}
		else
		{
			UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, "777", "UDP", NULL);
			std::cout << "UPNP: Port 777 closed." << std::endl;
		}
		FreeUPNPUrls(&urls);
	}
	freeUPNPDevlist(devlist);
}

