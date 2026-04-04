#include "../network/Client.h"
#include "../Config.h"
#include "../network/ESP32_Code.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../rendering/Render.h"
#include "../network/SimulationServer.h"
#include "../racing/RaceManager.h"
#include <cstring>
#include <windows.h>
#include <chrono>

#if NETWORKING_ENABLED

// ============================================================================
// CLIENT STATE MANAGEMENT
// ============================================================================

enum class ClientState {
	DISCONNECTED,
	CONNECTING,
	AUTHENTICATING,
	AUTHENTICATED,
	REQUESTING_MAP,
	RECEIVING_MAP,
	MAP_LOADED,
	READY
};

// CLIENT GLOBALS
HSteamNetConnection g_connection_handle = k_HSteamNetConnection_Invalid;
bool g_is_client_running = false;
bool g_should_close_client = false;

// ✅ State tracking
ClientState g_client_state = ClientState::DISCONNECTED;
std::chrono::steady_clock::time_point g_last_packet_time;
std::chrono::steady_clock::time_point g_map_request_time;
int g_map_retry_count = 0;

// ✅ Map reception buffer
struct MapReceptionState {
	uint32_t expected_packets = 0;
	uint32_t server_timestamp = 0;
	std::vector<MapPointsPacket> received_packets;
	std::vector<bool> packet_received_flags;

	void reset() {
		expected_packets = 0;
		server_timestamp = 0;
		received_packets.clear();
		packet_received_flags.clear();
	}

	bool allReceived() const {
		if (packet_received_flags.empty()) return false;
		for (bool flag : packet_received_flags) {
			if (!flag) return false;
		}
		return true;
	}
};

MapReceptionState g_map_reception;

// ✅ External dependencies
extern MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;
extern std::atomic<bool> g_is_server_mode;
extern std::atomic<bool> g_is_client_mode;

// ============================================================================
// CONNECTION CALLBACKS
// ============================================================================

// External variables from main.cpp
extern std::vector<SplinePoint> g_smooth_track_points;
extern MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;
extern std::mutex g_vehicles_mutex;
extern std::mutex g_track_mutex;

// Track synchronization state
static bool g_track_header_received = false;
static uint32_t g_expected_track_points = 0;
static uint32_t g_received_track_points = 0;
static std::vector<SplinePoint> g_received_track_buffer;

static glm::vec2 g_start_finish_p1(0.0f, 0.0f);
static glm::vec2 g_start_finish_p2(0.0f, 0.0f);

// UI-provided connection params
static std::mutex g_client_params_mutex;
static std::string g_connect_host;
static uint16_t g_connect_port = 0;
static std::string g_connect_password;

// UI-visible auth state
static std::atomic<bool> g_client_authenticated{ false };
static std::atomic<bool> g_client_had_auth_failure{ false };

bool clientIsAuthenticated() { return g_client_authenticated.load(); }
bool clientHadAuthFailure() { return g_client_had_auth_failure.load(); }
void clientClearAuthState()
{
	g_client_authenticated.store(false);
	g_client_had_auth_failure.store(false);
}

void clientSetConnectParams(const char* host, uint16_t port, const char* password_or_null)
{
	std::lock_guard<std::mutex> lock(g_client_params_mutex);
	g_connect_host = host ? host : "";
	g_connect_port = port;
	g_connect_password = password_or_null ? password_or_null : "";
}

void clientGetConnectParams(std::string& out_host, uint16_t& out_port, std::string& out_password)
{
	std::lock_guard<std::mutex> lock(g_client_params_mutex);
	out_host = g_connect_host;
	out_port = g_connect_port;
	out_password = g_connect_password;
}

void onClientConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	switch (pInfo->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connected:
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_GREEN);
		std::cout << "✅ Connected to Server" << std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

		g_client_state = ClientState::CONNECTED;
		g_last_packet_time = std::chrono::steady_clock::now();
		break;

	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
		std::cout << "❌ Connection lost: " << pInfo->m_info.m_szEndDebug << std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

		SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
		g_connection_handle = k_HSteamNetConnection_Invalid;
		g_client_state = ClientState::DISCONNECTED;
		g_map_reception.reset();
		break;
	}
}

// ============================================================================
// AUTHENTICATION
// ============================================================================

static bool sendAuthPacket(HSteamNetConnection connection, const std::string& password)
{
	if (connection == k_HSteamNetConnection_Invalid) {
		std::cerr << "[CLIENT] ❌ Cannot send auth - invalid connection" << std::endl;
		return false;
	}

	AuthPacket auth_packet;
	auth_packet.magic_marker = PacketMagic::AUTH;
	strncpy_s(auth_packet.password, password.c_str(), sizeof(auth_packet.password) - 1);
	auth_packet.password[sizeof(auth_packet.password) - 1] = '\0';

	EResult result = SteamNetworkingSockets()->SendMessageToConnection(
		connection, &auth_packet, sizeof(auth_packet),
		k_nSteamNetworkingSend_Reliable, nullptr
	);

	if (result == k_EResultOK) {
		std::cout << "[CLIENT] 📨 Authentication packet sent" << std::endl;
		return true;
	}

	std::cerr << "[CLIENT] ❌ Failed to send authentication packet" << std::endl;
	return false;
}

// ============================================================================
// MAP SYNCHRONIZATION
// ============================================================================

static bool requestMapFromServer()
{
	if (g_connection_handle == k_HSteamNetConnection_Invalid) {
		std::cerr << "[CLIENT] ❌ Cannot request map - invalid connection" << std::endl;
		return false;
	}

	MapRequestPacket map_request;
	map_request.client_timestamp = static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count()
	);

	EResult result = SteamNetworkingSockets()->SendMessageToConnection(
		g_connection_handle, &map_request, sizeof(map_request),
		k_nSteamNetworkingSend_Reliable, nullptr
	);

	if (result == k_EResultOK) {
		std::cout << "[CLIENT] 📡 Requesting map data from server..." << std::endl;
		g_map_request_time = std::chrono::steady_clock::now();
		g_client_state = ClientState::REQUESTING_MAP;
		return true;
	}

	std::cerr << "[CLIENT] ❌ Failed to send map request" << std::endl;
	return false;
}

static void assembleAndLoadTrack(const std::vector<glm::vec2>& track_points, std::mutex& points_mutex)
{
	std::cout << "[CLIENT] 🔨 Assembling track from " << g_map_reception.expected_packets << " packets..." << std::endl;

	// Build CSV string from packets
	std::string track_data_str;
	for (uint32_t i = 0; i < g_map_reception.expected_packets; ++i)
	{
		const MapPointsPacket& packet = g_map_reception.received_packets[i];
		for (uint32_t j = 0; j < packet.num_points; ++j)
		{
			track_data_str += std::to_string(packet.points[j].lat_dd) + "," 
						   + std::to_string(packet.points[j].lon_dd) + "\n";
		}
	}

	// Load using existing function
	loadTrackFromData(track_data_str, const_cast<std::vector<glm::vec2>&>(track_points), points_mutex);

	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_GREEN);
	std::cout << "[CLIENT] ✅ Track loaded! Ready to receive telemetry." << std::endl;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

	g_client_state = ClientState::READY;
	g_map_reception.reset();
}

void connectToServer(const SteamNetworkingIPAddr& server_address)
{
	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)onClientConnectionStatusChanged);
	g_connection_handle = SteamNetworkingSockets()->ConnectByIPAddress(server_address, 1, &opt);

	if (g_connection_handle == k_HSteamNetConnection_Invalid)
	{
		std::cerr << "[CLIENT] ❌ Failed to initiate connection to server." << std::endl;
		return;
	}

	g_client_state = ClientState::CONNECTING;
	g_last_packet_time = std::chrono::steady_clock::now();
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

// ============================================================================
<<<<<<< HEAD
// MESSAGE PROCESSING FROM SERVER
// ============================================================================

void listenMessagesFromServer(const std::vector<glm::vec2>& track_points, std::mutex& points_mutex)
{
	ISteamNetworkingMessage* pIncomingMsg[16];
	int numMsgs = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_connection_handle, pIncomingMsg, 16);

	if (numMsgs > 0) {
		g_last_packet_time = std::chrono::steady_clock::now();  // ✅ Update heartbeat
	}

	for (int i = 0; i < numMsgs; ++i)
	{
		auto* message = pIncomingMsg[i];

		// Check magic marker
		if (message->m_cbSize >= sizeof(uint32_t)) {
			uint32_t* magic = (uint32_t*)message->m_pData;

			// ===== AUTH RESPONSE =====
			if (*magic == PacketMagic::RESP && message->m_cbSize == sizeof(AuthResponsePacket))
			{
				AuthResponsePacket* response = (AuthResponsePacket*)message->m_pData;

				if (response->is_authenticated)
				{
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_GREEN);
					std::cout << "[CLIENT] ✅ Authentication successful!" << std::endl;
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

					g_client_state = ClientState::AUTHENTICATED;

					// ✅ IMMEDIATELY request map after auth
					requestMapFromServer();
				}
				else
				{
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
					std::cout << "[CLIENT] ❌ Authentication failed: " << response->message << std::endl;
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

					if (response->attempts_remaining <= 0) {
						g_should_close_client = true;
					}
				}

				message->Release();
				continue;
			}

			// ===== MAP DATA HEADER =====
			if (*magic == PacketMagic::MAP_DATA && message->m_cbSize == sizeof(MapDataPacket))
			{
				MapDataPacket* map_data = (MapDataPacket*)message->m_pData;

				std::cout << "[CLIENT] 📦 Receiving map metadata:" << std::endl;
				std::cout << "  Origin: (" << map_data->origin_lat_dd << ", " << map_data->origin_lon_dd << ")" << std::endl;
				std::cout << "  Zone: " << map_data->origin_zone_int << map_data->origin_zone_char << std::endl;
				std::cout << "  Total points: " << map_data->total_points << std::endl;
				std::cout << "  Total packets: " << map_data->total_packets << std::endl;

				// Copy origin data
				g_map_origin.m_origin_lat_dd = map_data->origin_lat_dd;
				g_map_origin.m_origin_lon_dd = map_data->origin_lon_dd;
				g_map_origin.m_origin_meters_easting = map_data->origin_meters_easting;
				g_map_origin.m_origin_meters_northing = map_data->origin_meters_northing;
				g_map_origin.m_origin_zone_int = map_data->origin_zone_int;
				g_map_origin.m_origin_zone_char = map_data->origin_zone_char;
				g_map_origin.m_map_size = map_data->map_size;

				// Prepare reception buffer
				g_map_reception.expected_packets = map_data->total_packets;
				g_map_reception.server_timestamp = map_data->server_timestamp;
				g_map_reception.received_packets.resize(map_data->total_packets);
				g_map_reception.packet_received_flags.resize(map_data->total_packets, false);

				g_client_state = ClientState::RECEIVING_MAP;

				message->Release();
				continue;
			}

			// ===== MAP POINTS =====
			if (*magic == PacketMagic::MAP_POINTS && message->m_cbSize == sizeof(MapPointsPacket))
			{
				MapPointsPacket* points_packet = (MapPointsPacket*)message->m_pData;

				// Validate sequence number
				if (points_packet->sequence_number >= g_map_reception.expected_packets) {
					std::cerr << "[CLIENT] ❌ Invalid sequence number: " << points_packet->sequence_number << std::endl;
					message->Release();
					continue;
				}

				// Check for duplicates (network retransmission)
				if (g_map_reception.packet_received_flags[points_packet->sequence_number]) {
					std::cout << "[CLIENT] ⚠️ Duplicate packet #" << points_packet->sequence_number << " ignored" << std::endl;
					message->Release();
					continue;
				}

				// Store packet
				g_map_reception.received_packets[points_packet->sequence_number] = *points_packet;
				g_map_reception.packet_received_flags[points_packet->sequence_number] = true;

				uint32_t received_count = 0;
				for (bool flag : g_map_reception.packet_received_flags) {
					if (flag) received_count++;
				}

				std::cout << "[CLIENT] 📦 MapPointsPacket " << (points_packet->sequence_number + 1) 
						  << "/" << points_packet->total_packets << " received (" 
						  << points_packet->num_points << " points) [" 
						  << received_count << "/" << g_map_reception.expected_packets << " total]" << std::endl;

				// Check if all received
				if (g_map_reception.allReceived())
				{
					std::cout << "[CLIENT] ✅ All map packets received! Assembling track..." << std::endl;
					assembleAndLoadTrack(track_points, points_mutex);
				}

				message->Release();
				continue;
			}

			// ===== TELEMETRY DATA =====
			if (*magic == PacketMagic::DATA && message->m_cbSize == sizeof(TelemetryPacket))
			{
				// Only process if map is loaded
				if (g_client_state == ClientState::READY && g_is_map_loaded)
				{
					TelemetryPacket* telemetry = (TelemetryPacket*)message->m_pData;
					processIncomingTelemetry(*telemetry);  // ✅ Use unified function!
				}
				// Silently ignore telemetry before map is ready

				message->Release();
				continue;
			}
		}

		// Unknown packet type
		message->Release();
	}

	// ===== TIMEOUT DETECTION =====
	auto now = std::chrono::steady_clock::now();

	// Connection timeout (no packets for N seconds)
	if (g_client_state != ClientState::DISCONNECTED && g_client_state != ClientState::CONNECTING)
	{
		auto time_since_last_packet = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - g_last_packet_time
		).count();

		if (time_since_last_packet > NetworkConstants::CONNECTION_TIMEOUT_MS)
		{
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
			std::cerr << "[CLIENT] ❌ Connection timeout - no packets for " 
					  << time_since_last_packet << "ms" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

			g_should_close_client = true;
		}
	}

	// Map request timeout (retry logic)
	if (g_client_state == ClientState::REQUESTING_MAP || g_client_state == ClientState::RECEIVING_MAP)
	{
		auto time_since_request = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - g_map_request_time
		).count();

		if (time_since_request > NetworkConstants::MAP_REQUEST_TIMEOUT_MS)
		{
			g_map_retry_count++;

			if (g_map_retry_count < NetworkConstants::MAX_MAP_RETRIES)
			{
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_YELLOW);
				std::cout << "[CLIENT] ⚠️ Map request timeout - retrying (" 
						  << g_map_retry_count << "/" << NetworkConstants::MAX_MAP_RETRIES << ")..." << std::endl;
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

				g_map_reception.reset();
				requestMapFromServer();
			}
			else
			{
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
				std::cerr << "[CLIENT] ❌ Map sync failed after " << NetworkConstants::MAX_MAP_RETRIES << " retries" << std::endl;
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

				g_should_close_client = true;
			}
=======
// PROCESS TRACK DATA FROM SERVER
// ============================================================================
static void processTrackHeader(const TrackDataHeader* header)
{
	std::cout << "[CLIENT] Receiving track: " << header->point_count << " points" << std::endl;

	// Update map origin (shared with render/telemetry threads)
	{
		std::lock_guard<std::mutex> lock(g_track_mutex);
		g_map_origin.m_origin_lat_dd = header->origin_lat;
		g_map_origin.m_origin_lon_dd = header->origin_lon;
		g_map_origin.m_origin_meters_easting = header->origin_easting;
		g_map_origin.m_origin_meters_northing = header->origin_northing;
		g_map_origin.m_origin_zone_int = header->origin_zone;
		g_map_origin.m_origin_zone_char = header->origin_zone_char;
	}
	
	// Prepare to receive track points
	g_expected_track_points = header->point_count;
	g_received_track_points = 0;
	g_received_track_buffer.clear();
	g_received_track_buffer.reserve(header->point_count);
	g_track_header_received = true;
	
  // Cache start/finish line
	g_start_finish_p1 = glm::vec2(header->start_finish_p1_x, header->start_finish_p1_y);
	g_start_finish_p2 = glm::vec2(header->start_finish_p2_x, header->start_finish_p2_y);

	std::cout << "[CLIENT] Map origin: (" << header->origin_lat << ", " << header->origin_lon << ")" << std::endl;
}

static void processTrackChunk(const TrackChunkPacket* chunk)
{
	if (!g_track_header_received) {
		std::cerr << "[CLIENT] Received track chunk before header!" << std::endl;
		return;
	}
	
	// Add points from chunk to buffer
	for (uint32_t i = 0; i < chunk->points_in_chunk; i++)
	{
		SplinePoint sp;
		sp.position.x = chunk->points[i].x;
		sp.position.y = chunk->points[i].y;
		sp.tangent.x = chunk->points[i].tangent_x;
		sp.tangent.y = chunk->points[i].tangent_y;
		g_received_track_buffer.push_back(sp);
	}
	
	g_received_track_points += chunk->points_in_chunk;
	
	std::cout << "[CLIENT] Chunk " << chunk->chunk_index << " received (" 
	          << g_received_track_points << "/" << g_expected_track_points << ")" << std::endl;
	
	// Check if all points received
	if (g_received_track_points >= g_expected_track_points)
	{
		// Transfer to main track (shared with main render thread)
		{
			std::lock_guard<std::mutex> lock(g_track_mutex);
			g_smooth_track_points = g_received_track_buffer;
			g_is_map_loaded = true;
		}

		// Ensure RaceManager has a valid line (so lap UI can work).
		if (g_race_manager)
		{
			g_race_manager->SetStartFinishLine(g_start_finish_p1, g_start_finish_p2);
		}

		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_GREEN);
		std::cout << "[CLIENT] ✓ Track fully loaded and ready (" << g_received_track_points << " points)" << std::endl;
		std::cout << "[CLIENT] Track will be rendered by main thread (OpenGL context)" << std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

		// ⚠️ NOTE: Cannot call OpenGL functions from network thread!
		// Track rendering cache will be built in main.cpp when it detects g_is_map_loaded = true

		// Reset state
		g_track_header_received = false;
		g_received_track_buffer.clear();
	}
}

static void processRaceData(const RaceDataPacket* race_data)
{
	std::cout << "[CLIENT] Race data received" << std::endl;
	std::cout << "[CLIENT] Has start/finish line: " << (race_data->has_start_finish_line ? "Yes" : "No") << std::endl;
	
	// Initialize RaceManager if needed
	// (this would be done in main.cpp normally)
}

void listenMessagesFromServer()
{
	ISteamNetworkingMessage* pIncomingMsg[16];
	while (true)
	{
		int numMsgs = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_connection_handle, pIncomingMsg, 16);
		if (numMsgs <= 0)
		{
			break;
		}

		for (int i = 0; i < numMsgs; ++i)
		{
			ISteamNetworkingMessage* msg = pIncomingMsg[i];
			uint32_t* magic = (uint32_t*)msg->m_pData;

			// Check message size to determine packet type
			if (msg->m_cbSize == sizeof(TrackDataHeader)) {
				if (*magic == PacketMagic::TRCK) {
					processTrackHeader((TrackDataHeader*)msg->m_pData);
					msg->Release();
					continue;
				}
			}
		
			if (msg->m_cbSize == sizeof(TrackChunkPacket)) {
				if (*magic == PacketMagic::TCHU) {
					processTrackChunk((TrackChunkPacket*)msg->m_pData);
					msg->Release();
					continue;
				}
			}
		
			if (msg->m_cbSize == sizeof(RaceDataPacket)) {
				if (*magic == PacketMagic::RACE) {
					processRaceData((RaceDataPacket*)msg->m_pData);
					msg->Release();
					continue;
				}
			}

			if (msg->m_cbSize == sizeof(VehicleStatePacket)) {
				if (*magic == PacketMagic::VSTA) {
					processIncomingVehicleState(*(VehicleStatePacket*)msg->m_pData);
					msg->Release();
					ErrnumMsg_b = false;
					continue;
				}
			}
		
			if (msg->m_cbSize == sizeof(TelemetryPacket)) {
                // Server-authoritative replication uses VehicleStatePacket.
				msg->Release();
				continue;
			}
		
			std::cout << "[CLIENT] Received unknown packet (size: " << msg->m_cbSize << ")" << std::endl;
			msg->Release();
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
		}
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

int clientStart(const std::vector<glm::vec2>& track_points, std::mutex& points_mutex)
{
<<<<<<< HEAD
	// ✅ Check mode exclusivity
	if (g_is_server_mode) {
		std::cerr << "[CLIENT] ❌ Cannot start client - server mode is active!" << std::endl;
		return 1;
	}
	g_is_client_mode = true;

	std::cout << "[CLIENT] Starting GNS Client..." << std::endl;
	SteamDatagramErrMsg error_message;
	std::string server_name;
	std::regex ip_port_pattern(R"(^(\d{1,3}\.){3}\d{1,3}:\d{1,5}$)");

	if (!GameNetworkingSockets_Init(nullptr, error_message)) {
		std::cerr << "[CLIENT] ❌ Failed to initialize GNS: " << error_message << std::endl;
		g_is_client_mode = false;
		return 1;
	}

	// ===== SERVER ADDRESS INPUT =====
	SteamNetworkingIPAddr server_address;
	server_address.Clear();
	std::cout << "[CLIENT] Enter server address (d=default, l=localhost, or IP:PORT): ";
	std::getline(std::cin, server_name);

	if (server_name == "d" || server_name == " " || server_name == "\n")
=======
	std::cout << "Starting GNS Client..." << std::endl;
  clientClearAuthState();
	SteamDatagramErrMsg error_message;
    std::regex ip_port_pattern(R"(^([A-Za-z0-9\-\.]+|localhost|(\d{1,3}\.){3}\d{1,3}):\d{1,5}$)");
	if (!GameNetworkingSockets_Init(nullptr, error_message)) return 1;

	std::string host;
	uint16_t port = 0;
	std::string password;
	clientGetConnectParams(host, port, password);

	SteamNetworkingIPAddr server_address;
	server_address.Clear();

	// Fallback to existing console flow if UI didn't provide anything
	std::string server_name;
	if (host.empty())
	{
		std::getline(std::cin, server_name);
	}
	else
	{
		server_name = host + ":" + std::to_string((port != 0) ? port : NetworkConstants::DEFAULT_SERVER_PORT);
	}

	if (server_name == "d" || server_name == " " || server_name == "\n" || server_name.empty())
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
	{
		std::string default_address = std::string(NetworkConstants::DEFAULT_SERVER_IP) + ":" 
									+ std::to_string(NetworkConstants::DEFAULT_SERVER_PORT);
		server_address.ParseString(default_address.c_str());
		std::cout << "[CLIENT] Using default server address: " << default_address << std::endl;
	}
	else if(std::regex_match(server_name, ip_port_pattern))
	{
		server_address.ParseString(server_name.c_str());
		std::cout << "[CLIENT] Using server address: " << server_name << std::endl;
	}
	else if (server_name == "l")
	{
		std::string local_server_ip = std::string(NetworkConstants::DEFAULT_LOCAL_SERVER_IP) + ":" 
									 + std::to_string(NetworkConstants::DEFAULT_SERVER_PORT);
		server_address.ParseString(local_server_ip.c_str());
		std::cout << "[CLIENT] Using localhost: " << local_server_ip << std::endl;
	}
	else
	{
		std::cerr << "[CLIENT] ❌ Invalid server address format." << std::endl;
		GameNetworkingSockets_Kill();
		g_is_client_mode = false;
		return 1; 
	}

	// ===== CONNECT TO SERVER =====
	connectToServer(server_address);

	std::cout << "[CLIENT] Waiting for connection..." << std::endl;
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	// ===== AUTHENTICATION LOOP =====
	bool is_authenticated = false;
	int auth_attempts = 0;

	while (!is_authenticated && auth_attempts < NetworkConstants::MAX_AUTH_ATTEMPTS && !g_should_close_client)
	{
<<<<<<< HEAD
		std::string password;
		std::cout << "[CLIENT] Enter server password: ";
		std::getline(std::cin, password);

		if (!sendAuthPacket(g_connection_handle, password)) {
			std::cerr << "[CLIENT] ❌ Failed to send authentication packet" << std::endl;
=======
      // If UI provided a host, keep polling current UI password for retries.
		std::string attempt_password;
		if (!host.empty())
		{
			std::string current_host;
			uint16_t current_port = 0;
			std::string current_password;
			clientGetConnectParams(current_host, current_port, current_password);
			attempt_password = current_password;
		}
		else
		{
			std::cout << "Enter server password: ";
			std::getline(std::cin, attempt_password);
		}
		
       if (!sendAuthPacket(g_connection_handle, attempt_password)) {
			std::cerr << "Failed to send authentication packet" << std::endl;
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
			break;
		}

		g_client_state = ClientState::AUTHENTICATING;
		auth_attempts++;

		// Wait for auth response
		std::cout << "[CLIENT] Authenticating..." << std::endl;
		int response_timeout = 0;
		const int authResponseMaxPolls = 5000 / NetworkConstants::AUTH_POLL_INTERVAL_MS;
		bool received_response = false;

<<<<<<< HEAD
		while (response_timeout < 50 && !received_response && !g_should_close_client) {
			SteamNetworkingSockets()->RunCallbacks();

=======
		while (response_timeout < authResponseMaxPolls && !received_response && !g_should_close_client) {
			SteamNetworkingSockets()->RunCallbacks();

			// ✅ CRITICAL FIX: Process ALL incoming packets (including track data)
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
			ISteamNetworkingMessage* messages[16];
			int msg_count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_connection_handle, messages, 16);

			for (int i = 0; i < msg_count; i++) {
<<<<<<< HEAD
				if (messages[i]->m_cbSize >= sizeof(uint32_t)) {
					uint32_t* magic = (uint32_t*)messages[i]->m_pData;

					if (*magic == PacketMagic::RESP && messages[i]->m_cbSize == sizeof(AuthResponsePacket)) {
						AuthResponsePacket* response = (AuthResponsePacket*)messages[i]->m_pData;
						received_response = true;

						if (response->is_authenticated) {
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_GREEN);
							std::cout << "[CLIENT] ✅ Authentication successful!" << std::endl;
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);
							is_authenticated = true;
						} else {
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
							std::cout << "[CLIENT] ❌ Authentication failed: " << response->message << std::endl;
							SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

							if (response->attempts_remaining <= 0) {
								g_should_close_client = true;
							}
=======
				ISteamNetworkingMessage* msg = messages[i];
				uint32_t* magic = (uint32_t*)msg->m_pData;

				// Handle auth response
				if (msg->m_cbSize == sizeof(AuthResponsePacket) && *magic == PacketMagic::RESP) {
					AuthResponsePacket* response = (AuthResponsePacket*)msg->m_pData;
					received_response = true;

					if (response->is_authenticated) {
						SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_GREEN);
						std::cout << "Authentication successful!" << std::endl;
						SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);
						is_authenticated = true;
                        g_client_authenticated.store(true);
					} else {
						SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
						std::cout << "Authentication failed: " << response->message << std::endl;
						SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);
						g_client_had_auth_failure.store(true);

						if (response->attempts_remaining <= 0) {
							std::cerr << "Max attempts exceeded - connection will close" << std::endl;
							g_should_close_client = true;
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
						}
					}
					msg->Release();
					continue;
				}

				// ✅ Handle track data packets
				if (msg->m_cbSize == sizeof(TrackDataHeader) && *magic == PacketMagic::TRCK) {
					std::cout << "[CLIENT] Processing TrackDataHeader during auth..." << std::endl;
					processTrackHeader((TrackDataHeader*)msg->m_pData);
					msg->Release();
					continue;
				}

				if (msg->m_cbSize == sizeof(TrackChunkPacket) && *magic == PacketMagic::TCHU) {
					processTrackChunk((TrackChunkPacket*)msg->m_pData);
					msg->Release();
					continue;
				}

				if (msg->m_cbSize == sizeof(RaceDataPacket) && *magic == PacketMagic::RACE) {
					processRaceData((RaceDataPacket*)msg->m_pData);
					msg->Release();
					continue;
				}

				if (msg->m_cbSize == sizeof(VehicleStatePacket) && *magic == PacketMagic::VSTA) {
					processIncomingVehicleState(*(VehicleStatePacket*)msg->m_pData);
					msg->Release();
					continue;
				}

				// Release any other packets
				msg->Release();
			}
<<<<<<< HEAD

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
=======
			
			std::this_thread::sleep_for(std::chrono::milliseconds(NetworkConstants::AUTH_POLL_INTERVAL_MS));
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
			response_timeout++;
		}

		if (!received_response && !is_authenticated) {
			std::cout << "[CLIENT] ⚠️ No response from server - retrying..." << std::endl;
		}

       // UI flow: wait for user to change password and press Connect again.
		if (!is_authenticated && !host.empty() && auth_attempts < NetworkConstants::MAX_AUTH_ATTEMPTS)
		{
			std::string last_password = attempt_password;
			while (!g_should_close_client && !is_authenticated)
			{
				// If the connection died, abort.
				SteamNetConnectionInfo_t info;
				SteamNetworkingSockets()->GetConnectionInfo(g_connection_handle, &info);
				if (info.m_eState != k_ESteamNetworkingConnectionState_Connected)
				{
					g_should_close_client = true;
					break;
				}

				std::string current_host;
				uint16_t current_port = 0;
				std::string current_password;
				clientGetConnectParams(current_host, current_port, current_password);
				if (current_password != last_password)
					break;

				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}
	}

	if (!is_authenticated) {
<<<<<<< HEAD
		std::cerr << "[CLIENT] ❌ Authentication failed. Closing connection." << std::endl;
=======
		std::cerr << "Authentication failed. Closing connection." << std::endl;
       g_is_client_running = false;
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
		GameNetworkingSockets_Kill();
		g_is_client_mode = false;
		return 1;
	}
<<<<<<< HEAD

	// ===== REQUEST MAP =====
	g_client_state = ClientState::AUTHENTICATED;
	if (!requestMapFromServer()) {
		std::cerr << "[CLIENT] ❌ Failed to request map" << std::endl;
		GameNetworkingSockets_Kill();
		g_is_client_mode = false;
		return 1;
	}

	// ===== MAIN LOOP =====
	std::cout << "[CLIENT] Entering main loop..." << std::endl;

	while (!g_should_close_client)
	{
		SteamNetworkingSockets()->RunCallbacks();
		listenMessagesFromServer(track_points, points_mutex);

		// ✅ Fast polling for smooth telemetry (16ms = ~60 FPS)
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	// ===== CLEANUP =====
	std::cout << "[CLIENT] Shutting down GNS Client..." << std::endl;
=======

	// Track should have been loaded during authentication
	if (g_is_map_loaded) {
		std::cout << "[CLIENT] ✓ Ready to race - track loaded, waiting for telemetry..." << std::endl;
	} else {
		std::cout << "[CLIENT] ⚠ Warning: Track not loaded - vehicles may not display correctly" << std::endl;
	}

	std::string message;
	int counter = 400;
	const int heartbeatThreshold = 60000 / NetworkConstants::CLIENT_POLL_INTERVAL_MS;
	while (!g_should_close_client)
	{

		SteamNetworkingSockets()->RunCallbacks();

		listenMessagesFromServer();

		if (counter >= heartbeatThreshold)
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

		std::this_thread::sleep_for(std::chrono::milliseconds(NetworkConstants::CLIENT_POLL_INTERVAL_MS));
		++counter;
	}

	std::cout << "Shutting down GNS Client..." << std::endl;
   g_is_client_running = false;
>>>>>>> b01485e8e2140bcc72ca97bce3f77ab1df53064d
	GameNetworkingSockets_Kill();
	g_is_client_mode = false;
	g_client_state = ClientState::DISCONNECTED;

	return 0;
}

#else

// ARM64 stub implementations
bool g_is_client_running = false;

int clientStart(const std::vector<glm::vec2>&, std::mutex&) { return 0; }
void clientStop() {}
void toggleClientRunning() {}
bool isClientRunning() { return false; }
void continueClientRunning() {}

#endif // NETWORKING_ENABLED

