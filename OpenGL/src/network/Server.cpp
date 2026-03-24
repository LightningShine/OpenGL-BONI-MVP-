#include "../network/Server.h"
#include "../Config.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../racing/RaceManager.h"
#include <cstring>
#include <windows.h>

// Console colors (anonymous namespace for internal use)
namespace {
	constexpr WORD CONSOLE_DEFAULT = 7;
	constexpr WORD CONSOLE_COLOR_GREEN = 10;
	constexpr WORD CONSOLE_COLOR_RED = 12;
	constexpr WORD CONSOLE_COLOR_YELLOW = 14;
}

// External variables from main.cpp
extern std::vector<SplinePoint> g_smooth_track_points;
extern std::mutex g_track_mutex;
extern MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;
extern RaceManager* g_race_manager;




#if NETWORKING_ENABLED

// SERVER 

HSteamListenSocket g_hListenSocket;
HSteamNetPollGroup g_hPollGroup;
std::vector<HSteamNetConnection> g_hConnections;
static std::map<HSteamNetConnection, bool> g_authenticated_connections;
static std::map<HSteamNetConnection, int> g_auth_attempts;

static std::mutex g_server_password_mutex;
static std::string g_server_password;

static std::mutex g_server_wanip_mutex;
static std::string g_server_wan_ip;

void serverSetPassword(const char* password_or_null)
{
	std::lock_guard<std::mutex> lock(g_server_password_mutex);
	g_server_password = password_or_null ? password_or_null : "";
}

const char* serverGetPassword()
{
	std::lock_guard<std::mutex> lock(g_server_password_mutex);
	return g_server_password.c_str();
}

const char* serverGetWanIp()
{
	std::lock_guard<std::mutex> lock(g_server_wanip_mutex);
	return g_server_wan_ip.c_str();
}

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

static uint32_t GetMonotonicServerTimeMs()
{
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
}

static VehicleStatePacket MakeVehicleStatePacket(const Vehicle& vehicle)
{
	VehicleStatePacket packet{};
	packet.magic_marker = PacketMagic::VSTA;
	packet.vehicle_id = vehicle.m_id;
	packet.server_time_ms = GetMonotonicServerTimeMs();
	packet.normalized_x = static_cast<float>(vehicle.m_normalized_x);
	packet.normalized_y = static_cast<float>(vehicle.m_normalized_y);
	packet.heading = static_cast<float>(vehicle.m_heading);
	packet.speed_kph = static_cast<float>(vehicle.m_speed_kph);
	packet.track_progress = static_cast<float>(vehicle.m_track_progress);
	packet.current_lap_time = vehicle.m_current_lap_timer;
	packet.best_lap_time = vehicle.m_best_lap_time;
	packet.completed_laps = vehicle.m_completed_laps;
	packet.current_lap_number = vehicle.m_current_lap_number;
	packet.has_started_first_lap = vehicle.m_has_started_first_lap ? 1 : 0;
	packet.is_leader = vehicle.m_is_leader ? 1 : 0;
	return packet;
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

void BroadcastVehicleStateToClients(const VehicleStatePacket& packet)
{
#if NETWORKING_ENABLED
	if (!g_is_server_running) return;

	bool has_authenticated_clients = false;
	for (const auto& pair : g_authenticated_connections) {
		if (pair.second) {
			has_authenticated_clients = true;
			break;
		}
	}

	if (has_authenticated_clients) {
		SendToAll(&packet, sizeof(VehicleStatePacket));
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
	
  // Verify password (empty server password => allow all)
	std::string expected;
	{
		std::lock_guard<std::mutex> lock(g_server_password_mutex);
		expected = g_server_password;
	}

	const char* provided = password ? password : "";
	if (expected.empty() || strcmp(provided, expected.c_str()) == 0)
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

		// Send track and race data after successful authentication
		SendTrackAndRaceData(connection);

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
// SEND TRACK AND RACE DATA TO CLIENT
// Called after successful authentication
// ============================================================================
void SendTrackAndRaceData(HSteamNetConnection connection)
{
	std::vector<SplinePoint> track_copy;
	{
		std::lock_guard<std::mutex> track_lock(g_track_mutex);
		track_copy = g_smooth_track_points;
	}

	if (!g_is_map_loaded || track_copy.empty()) {
		std::cout << "[SERVER] No track loaded - skipping track sync for client " << connection << std::endl;
		return;
	}

	std::cout << "[SERVER] Sending track data to client " << connection << "..." << std::endl;

	// 1. Send track header
	TrackDataHeader header;
	header.magic_marker = PacketMagic::TRCK;
	header.point_count = static_cast<uint32_t>(track_copy.size());
	header.origin_lat = g_map_origin.m_origin_lat_dd;
	header.origin_lon = g_map_origin.m_origin_lon_dd;
	header.origin_easting = g_map_origin.m_origin_meters_easting;
	header.origin_northing = g_map_origin.m_origin_meters_northing;
	header.origin_zone = g_map_origin.m_origin_zone_int;
	header.origin_zone_char = g_map_origin.m_origin_zone_char;

   // Start/finish line: best-effort.
	// If you don't have an explicit start/finish line, use the first track segment.
	if (track_copy.size() >= 2)
	{
		header.start_finish_p1_x = track_copy[0].position.x;
		header.start_finish_p1_y = track_copy[0].position.y;
		header.start_finish_p2_x = track_copy[1].position.x;
		header.start_finish_p2_y = track_copy[1].position.y;
	}
	else
	{
		header.start_finish_p1_x = 0.0f;
		header.start_finish_p1_y = 0.0f;
		header.start_finish_p2_x = 0.0f;
		header.start_finish_p2_y = 0.0f;
	}

	EResult result = SteamNetworkingSockets()->SendMessageToConnection(
		connection,
		&header,
		sizeof(header),
		k_nSteamNetworkingSend_Reliable,
		nullptr
	);

	if (result != k_EResultOK) {
		std::cerr << "[SERVER] Failed to send track header to client " << connection << std::endl;
		return;
	}

	std::cout << "[SERVER] Track header sent: " << header.point_count << " points" << std::endl;

	// 2. Send track points in chunks
	uint32_t total_points = static_cast<uint32_t>(track_copy.size());
	uint32_t chunks_needed = (total_points + MAX_POINTS_PER_CHUNK - 1) / MAX_POINTS_PER_CHUNK;

	for (uint32_t chunk_idx = 0; chunk_idx < chunks_needed; chunk_idx++)
	{
		TrackChunkPacket chunk;
		chunk.magic_marker = PacketMagic::TCHU;
		chunk.chunk_index = chunk_idx;

		uint32_t start_idx = chunk_idx * MAX_POINTS_PER_CHUNK;
		uint32_t end_idx = std::min(start_idx + MAX_POINTS_PER_CHUNK, total_points);
		chunk.points_in_chunk = end_idx - start_idx;

		// Copy points to chunk
		for (uint32_t i = 0; i < chunk.points_in_chunk; i++)
		{
			const SplinePoint& sp = track_copy[start_idx + i];
			chunk.points[i].x = sp.position.x;
			chunk.points[i].y = sp.position.y;
			chunk.points[i].tangent_x = sp.tangent.x;
			chunk.points[i].tangent_y = sp.tangent.y;
		}

		result = SteamNetworkingSockets()->SendMessageToConnection(
			connection,
			&chunk,
			sizeof(chunk),
			k_nSteamNetworkingSend_Reliable,
			nullptr
		);

		if (result != k_EResultOK) {
			std::cerr << "[SERVER] Failed to send track chunk " << chunk_idx << " to client " << connection << std::endl;
			return;
		}

		// Small delay between chunks to avoid flooding
		if (chunk_idx % 10 == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	std::cout << "[SERVER] All track chunks sent (" << chunks_needed << " chunks)" << std::endl;

	// 3. Send race data
	RaceDataPacket race_data;
	race_data.magic_marker = PacketMagic::RACE;
	race_data.has_start_finish_line = (g_race_manager != nullptr);

	result = SteamNetworkingSockets()->SendMessageToConnection(
		connection,
		&race_data,
		sizeof(race_data),
		k_nSteamNetworkingSend_Reliable,
		nullptr
	);

	if (result == k_EResultOK) {
		std::cout << "[SERVER] Race data sent to client " << connection << std::endl;
	} else {
		std::cerr << "[SERVER] Failed to send race data to client " << connection << std::endl;
	}

	// 4. Send current processed vehicle states once so a newly connected client does
	// not need to wait for the next simulation tick to see cars in the right place.
	{
		std::lock_guard<std::mutex> vehicle_lock(g_vehicles_mutex);
		for (const auto& [vehicleId, vehicle] : g_vehicles)
		{
			VehicleStatePacket statePacket = MakeVehicleStatePacket(vehicle);
			SteamNetworkingSockets()->SendMessageToConnection(
				connection,
				&statePacket,
				sizeof(statePacket),
				k_nSteamNetworkingSend_Reliable,
				nullptr
			);
		}
	}

	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), CONSOLE_COLOR_GREEN);
	std::cout << "[SERVER] ✓ Client " << connection << " fully synchronized" << std::endl;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), CONSOLE_DEFAULT);
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

bool isServerListening()
{
	return g_hListenSocket != k_HSteamListenSocket_Invalid;
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
	{
		// Ensure WAN IP is populated even if mapping fails but IGD discovery succeeded.
		std::lock_guard<std::mutex> lock(g_server_wanip_mutex);
		if (!g_server_wan_ip.empty())
			std::cout << "UPNP: WAN IP: " << g_server_wan_ip << std::endl;
	}

	std::cout << "[SERVER] Ready to broadcast telemetry from real/simulated vehicles" << std::endl;
	std::cout << "[SERVER] Raw telemetry remains available; simulation now broadcasts processed vehicle states" << std::endl;

	while (!ServerNeedStop_b)
	{
		FrameUpdate();
		std::this_thread::sleep_for(std::chrono::milliseconds(NetworkConstants::SERVER_POLL_INTERVAL_MS));
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
	if (!devlist)
	{
		std::cout << "UPNP: No devices found (error=" << error << ")" << std::endl;
		return;
	}

	struct UPNPUrls urls{};
	struct IGDdatas data{};
	char lanaddr[64] = { 0 };
	char wanaddr[64] = { 0 };

	int status = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr), wanaddr, sizeof(wanaddr));
	if (status == 0)
	{
		std::cout << "UPNP: No valid IGD found" << std::endl;
		freeUPNPDevlist(devlist);
		return;
	}

	if (open)
	{
       int r = UPNP_AddPortMapping(
			urls.controlURL,
			data.first.servicetype,
			"777",
			"777",
			lanaddr,
			"MyTelemetryServer",
			"UDP",
			NULL,
			"86400");

		if (r != UPNPCOMMAND_SUCCESS)
		{
			std::cout << "UPNP: AddPortMapping failed with code: " << r << " status: " << status << std::endl;
			std::cout << "UPNP: LAN IP: " << lanaddr << " WAN IP: " << wanaddr << std::endl;
		}
		else
		{
			std::cout << "UPNP: Port 777 opened automatically" << std::endl;
		}
	}
   else
	{
		int r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, "777", "UDP", NULL);
		if (r != UPNPCOMMAND_SUCCESS)
			std::cout << "UPNP: DeletePortMapping failed with code: " << r << std::endl;
		else
			std::cout << "UPNP: Port 777 closed" << std::endl;
	}
	FreeUPNPUrls(&urls);
	freeUPNPDevlist(devlist);
}

#else

// ARM64 stub implementations
bool g_is_server_running = false;

int serverWork() { return 0; }
bool isServerRunning() { return false; }
void ChangeisServerRunning() {}
void serverStop() {}
void continueServerRunning() {}
void BroadcastTelemetryToClients(const TelemetryPacket& packet) {} // ARM64 stub
void BroadcastVehicleStateToClients(const VehicleStatePacket& packet) {} // ARM64 stub

#endif // NETWORKING_ENABLED

