#include "../network/Server.h"
#include "../Config.h"
#include "../input/Input.h"  // ✅ For MapOrigin and coordinate functions
#include <cstring>
#include <windows.h>
#include <glm/glm.hpp>       // ✅ For glm::vec2
#include <GeographicLib/UTMUPS.hpp>  // ✅ For GPS conversion

// Console colors (anonymous namespace for internal use)
namespace {
    constexpr WORD CONSOLE_DEFAULT = 7;
    constexpr WORD CONSOLE_COLOR_GREEN = 10;
    constexpr WORD CONSOLE_COLOR_RED = 12;
    constexpr WORD CONSOLE_COLOR_YELLOW = 14;
}




#if NETWORKING_ENABLED

// ============================================================================
// GLOBAL STATE
// ============================================================================

// SERVER 
HSteamListenSocket g_hListenSocket;
HSteamNetPollGroup g_hPollGroup;
std::vector<HSteamNetConnection> g_hConnections;
static std::map<HSteamNetConnection, bool> g_authenticated_connections;
static std::map<HSteamNetConnection, int> g_auth_attempts;

bool g_is_server_running = false;
bool ServerNeedStop_b = false;

// ✅ Mode flags (prevent server and client running simultaneously)
std::atomic<bool> g_is_server_mode(false);
std::atomic<bool> g_is_client_mode(false);

// ✅ External dependencies (defined in Input.cpp and Vehicle.cpp)
extern class MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;
extern std::mutex g_vehicles_mutex;


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

// ============================================================================
// BROADCAST TELEMETRY TO CLIENTS (called from processIncomingTelemetry)
// ============================================================================
void BroadcastTelemetryToClients(const TelemetryPacket& packet)
{
#if NETWORKING_ENABLED
	// Only broadcast if server is running and has authenticated clients
	if (!g_is_server_running) return;

	bool has_authenticated_clients = false;
	for (const auto& pair : g_authenticated_connections) {
		if (pair.second) {
			has_authenticated_clients = true;
			break;
		}
	}

	if (has_authenticated_clients) {
		SendToAll(&packet, sizeof(TelemetryPacket));
	}
#endif
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

// ============================================================================
// ✅ MAP SYNCHRONIZATION - Send track data to requesting client
// ============================================================================

void sendMapDataToClient(HSteamNetConnection connection, 
						 const std::vector<glm::vec2>& track_points,
						 std::mutex& points_mutex)
{
	// Guard clause: check if track is loaded
	if (!g_is_map_loaded || track_points.empty()) {
		std::cerr << "[SERVER] ❌ Cannot send map - not loaded yet!" << std::endl;
		return;
	}

	std::cout << "[SERVER] 📡 Sending map data to client (connection " 
			  << connection << ")..." << std::endl;

	uint32_t server_timestamp = static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count()
	);

	// ===== STEP 1: Send MapDataPacket with origin info =====
	MapDataPacket map_header;
	map_header.origin_lat_dd = g_map_origin.m_origin_lat_dd;
	map_header.origin_lon_dd = g_map_origin.m_origin_lon_dd;
	map_header.origin_meters_easting = g_map_origin.m_origin_meters_easting;
	map_header.origin_meters_northing = g_map_origin.m_origin_meters_northing;
	map_header.origin_zone_int = g_map_origin.m_origin_zone_int;
	map_header.origin_zone_char = g_map_origin.m_origin_zone_char;
	map_header.map_size = g_map_origin.m_map_size;
	map_header.server_timestamp = server_timestamp;

	std::vector<glm::vec2> points_copy;
	{
		std::lock_guard<std::mutex> lock(points_mutex);
		points_copy = track_points;  // Copy to avoid long lock
		map_header.total_points = static_cast<uint32_t>(track_points.size());
	}

	const int MAX_POINTS = NetworkConstants::MAX_POINTS_PER_MAP_PACKET;
	map_header.total_packets = (map_header.total_points + MAX_POINTS - 1) / MAX_POINTS;

	EResult result = SteamNetworkingSockets()->SendMessageToConnection(
		connection, &map_header, sizeof(map_header),
		k_nSteamNetworkingSend_Reliable, nullptr
	);

	if (result != k_EResultOK) {
		std::cerr << "[SERVER] ❌ Failed to send MapDataPacket!" << std::endl;
		return;
	}

	std::cout << "[SERVER] ✅ MapDataPacket sent (origin + " 
			  << map_header.total_points << " points in " 
			  << map_header.total_packets << " packets)" << std::endl;

	// ===== STEP 2: Send track points in chunks =====
	for (uint32_t packet_idx = 0; packet_idx < map_header.total_packets; ++packet_idx)
	{
		MapPointsPacket points_packet;
		points_packet.sequence_number = packet_idx;
		points_packet.total_packets = map_header.total_packets;
		points_packet.server_timestamp = server_timestamp;

		uint32_t start_idx = packet_idx * MAX_POINTS;
		uint32_t end_idx = std::min(start_idx + MAX_POINTS, map_header.total_points);
		points_packet.num_points = end_idx - start_idx;

		// Convert normalized coordinates back to GPS
		for (uint32_t i = start_idx; i < end_idx; ++i)
		{
			uint32_t local_idx = i - start_idx;

			// Reverse: normalized → meters → GPS
			double meters_easting = g_map_origin.m_origin_meters_easting + 
								   (points_copy[i].x * g_map_origin.m_map_size);
			double meters_northing = g_map_origin.m_origin_meters_northing + 
									(points_copy[i].y * g_map_origin.m_map_size);

			try {
				using namespace GeographicLib;
				bool northp = (g_map_origin.m_origin_zone_char >= 'N');
				UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp,
							   meters_easting, meters_northing,
							   points_packet.points[local_idx].lat_dd,
							   points_packet.points[local_idx].lon_dd);
			}
			catch (const std::exception& e) {
				std::cerr << "[SERVER] GPS conversion error: " << e.what() << std::endl;
				points_packet.points[local_idx].lat_dd = 0;
				points_packet.points[local_idx].lon_dd = 0;
			}
		}

		// Send packet (reliable for map data)
		EResult chunk_result = SteamNetworkingSockets()->SendMessageToConnection(
			connection, &points_packet, sizeof(points_packet),
			k_nSteamNetworkingSend_Reliable, nullptr
		);

		if (chunk_result != k_EResultOK) {
			std::cerr << "[SERVER] ❌ Failed to send MapPointsPacket #" 
					  << packet_idx << "!" << std::endl;
		} else {
			std::cout << "[SERVER] 📦 MapPointsPacket " << (packet_idx + 1) 
					  << "/" << map_header.total_packets << " sent (" 
					  << points_packet.num_points << " points)" << std::endl;
		}
	}

	std::cout << "[SERVER] ✅ Map sync complete!" << std::endl;
}

// ✅ Handle map request from client
void handleMapRequest(HSteamNetConnection connection,
					  const std::vector<glm::vec2>& track_points,
					  std::mutex& points_mutex)
{
	std::cout << "[SERVER] 📨 Client " << connection 
			  << " requested map data" << std::endl;

	sendMapDataToClient(connection, track_points, points_mutex);
}

void FrameUpdate(const std::vector<glm::vec2>& track_points, std::mutex& points_mutex)
{
	SteamNetworkingSockets()->RunCallbacks();

	ISteamNetworkingMessage* incoming_messages[16];
	int message_count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(g_hPollGroup, incoming_messages, 16);

	for (int i = 0; i < message_count; ++i)
	{
		auto* message = incoming_messages[i];
		HSteamNetConnection connection = message->GetConnection();

		// ✅ Check packet type by magic marker
		if (message->m_cbSize >= sizeof(uint32_t)) {
			uint32_t* magic = (uint32_t*)message->m_pData;

			// ✅ AUTH packet
			if (*magic == PacketMagic::AUTH && message->m_cbSize == sizeof(AuthPacket)) {
				AuthPacket* auth_packet = (AuthPacket*)message->m_pData;
				authenticateConnection(connection, auth_packet->password);
				message->Release();
				continue;
			}

			// ✅ MAP_REQUEST packet (only from authenticated clients)
			if (*magic == PacketMagic::MAP_REQUEST && message->m_cbSize == sizeof(MapRequestPacket)) {
				if (g_authenticated_connections[connection]) {
					handleMapRequest(connection, track_points, points_mutex);
				} else {
					std::cout << "[SERVER] ❌ Rejected map request from unauthenticated client" << std::endl;
				}
				message->Release();
				continue;
			}
		}

		// Auth packet (legacy check)
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

		// Process authenticated message (other packet types)
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

int serverWork(const std::vector<glm::vec2>& track_points, std::mutex& points_mutex)
{
	// ✅ Set server mode flag (prevents client from starting)
	if (g_is_client_mode) {
		std::cerr << "[SERVER] ❌ Cannot start server - client mode is active!" << std::endl;
		return 1;
	}
	g_is_server_mode = true;

	std::cout << "Starting GNS Server..." << std::endl;
	SteamDatagramErrMsg errMsg;


	if (!GameNetworkingSockets_Init(nullptr, errMsg))
	{
		std::cerr << "Failed to initialize GNS: " << errMsg << std::endl;
		g_is_server_mode = false;  // ✅ Reset flag on failure
		return 1;
	}

	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, DebugOutput);

	std::cout << "GNS initialized successfully." << std::endl;
	ManagePort(true);
	StartServer(NetworkConstants::DEFAULT_SERVER_PORT);

	std::cout << "[SERVER] Ready to broadcast telemetry from real/simulated vehicles" << std::endl;
	std::cout << "[SERVER] Telemetry will be sent via processIncomingTelemetry() -> BroadcastTelemetryToClients()" << std::endl;
	std::cout << "[SERVER] Telemetry send rate: " << NetworkConstants::TELEMETRY_SEND_RATE_HZ << " Hz (every " 
			  << NetworkConstants::TELEMETRY_SEND_INTERVAL_MS << "ms)" << std::endl;

	while (!ServerNeedStop_b)
	{
		FrameUpdate(track_points, points_mutex);  // ✅ Pass track data for map sync
		std::this_thread::sleep_for(std::chrono::milliseconds(NetworkConstants::TELEMETRY_SEND_INTERVAL_MS));
	}

	ManagePort(false);
	GameNetworkingSockets_Kill();
	g_is_server_mode = false;  // ✅ Reset flag on shutdown
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

#else

// ARM64 stub implementations
bool g_is_server_running = false;
std::atomic<bool> g_is_server_mode(false);
std::atomic<bool> g_is_client_mode(false);

int serverWork(const std::vector<glm::vec2>&, std::mutex&) { return 0; }
bool isServerRunning() { return false; }
void ChangeisServerRunning() {}
void serverStop() {}
void continueServerRunning() {}
void BroadcastTelemetryToClients(const TelemetryPacket& packet) {} // ARM64 stub

#endif // NETWORKING_ENABLED

