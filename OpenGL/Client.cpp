#include "../network/Client.h"
#include "../Config.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../rendering/Render.h"
#include "../network/ESP32_Code.h"
#include "../network/SimulationServer.h"
#include <cstring>
#include <windows.h>

#if NETWORKING_ENABLED

// CLIENT 

HSteamNetConnection g_connection_handle;

bool g_is_client_running = false;
bool g_should_close_client = false;
bool ErrnumMsg_b = false;

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

// ============================================================================
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
				TelemetryPacket* pData = (TelemetryPacket*)msg->m_pData;

				// Fallback path for raw telemetry (kept for real devices / compatibility)
				processIncomingTelemetry(*pData);

				msg->Release();
				ErrnumMsg_b = false;
				continue;
			}
		
			std::cout << "[CLIENT] Received unknown packet (size: " << msg->m_cbSize << ")" << std::endl;
			msg->Release();
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
		const int authResponseMaxPolls = 5000 / NetworkConstants::AUTH_POLL_INTERVAL_MS;
		bool received_response = false;

		while (response_timeout < authResponseMaxPolls && !received_response && !g_should_close_client) {
			SteamNetworkingSockets()->RunCallbacks();

			// ✅ CRITICAL FIX: Process ALL incoming packets (including track data)
			ISteamNetworkingMessage* messages[16];
			int msg_count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(g_connection_handle, messages, 16);

			for (int i = 0; i < msg_count; i++) {
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
					} else {
						SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_COLOR_RED);
						std::cout << "Authentication failed: " << response->message << std::endl;
						SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), ConsoleColors::CONSOLE_DEFAULT);

						if (response->attempts_remaining <= 0) {
							std::cerr << "Max attempts exceeded - connection will close" << std::endl;
							g_should_close_client = true;
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
			
			std::this_thread::sleep_for(std::chrono::milliseconds(NetworkConstants::AUTH_POLL_INTERVAL_MS));
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

