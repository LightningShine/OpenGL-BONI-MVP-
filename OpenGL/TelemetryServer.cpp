#include "TelemetryServer.h"
#include <nlohmann/json.hpp>
#include <iostream>

#ifdef _WIN32
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "Ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <arpa/inet.h>
    #define SOCKET int
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define closesocket close
#endif

using json = nlohmann::json;

// Get local IP address (not 127.0.0.1)
static std::string GetLocalIP()
{
#ifdef _WIN32
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR)
    {
        return "Unknown";
    }

    struct addrinfo hints{}, *info;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, nullptr, &hints, &info) != 0)
    {
        return "Unknown";
    }

    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in* addr = (struct sockaddr_in*)info->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    
    freeaddrinfo(info);
    return std::string(ip);
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1)
    {
        return "Unknown";
    }

    std::string result = "Unknown";
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        
        // Skip localhost
        if (std::string(ip) != "127.0.0.1")
        {
            result = ip;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return result;
#endif
}

TelemetryServer::TelemetryServer()
    : m_running(false)
    , m_port(TELEMETRY_DEFAULT_PORT)
    , m_callback(nullptr)
{
}

TelemetryServer::~TelemetryServer()
{
    Stop();
}

bool TelemetryServer::Start(uint16_t port)
{
    if (m_running)
    {
        std::cout << "[TelemetryServer] Already running!\n";
        return false;
    }

    m_port = port;
    m_running = true;
    m_serverThread = std::thread(&TelemetryServer::ServerLoop, this);
    
    return true;
}

void TelemetryServer::Stop()
{
    if (!m_running)
    {
        return;
    }

    m_running = false;
    
    if (m_serverThread.joinable())
    {
        m_serverThread.join();
    }
    
    std::cout << "[TelemetryServer] Stopped\n";
}

bool TelemetryServer::IsRunning() const
{
    return m_running;
}

void TelemetryServer::SetCallback(TelemetryCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callback = callback;
}

void TelemetryServer::ServerLoop()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[TelemetryServer] ERROR: WSAStartup failed\n";
        m_running = false;
        return;
    }
#endif

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "[TelemetryServer] ERROR: Failed to create socket\n";
        m_running = false;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    // Set socket timeout for non-blocking checks
#ifdef _WIN32
    DWORD timeout = 1000; // 1 second in milliseconds
    setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(m_port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "[TelemetryServer] ERROR: Failed to bind on port " << m_port << "\n";
        std::cerr << "[TelemetryServer] Port might be in use. Try another port.\n";
        closesocket(serverSocket);
        m_running = false;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    // Get local IP for display
    std::string localIP = GetLocalIP();

    // Server successfully started
    std::cout << "\n";
    std::cout << "==========================================\n";
    std::cout << "    [TelemetryServer] STARTED OK\n";
    std::cout << "==========================================\n";
    std::cout << "  Protocol: UDP\n";
    std::cout << "  Port:     " << m_port << "\n";
    std::cout << "  Local IP: " << localIP << ":" << m_port << "\n";
    std::cout << "==========================================\n";
    std::cout << "  For external access configure port\n";
    std::cout << "  forwarding on your router.\n";
    std::cout << "  Check your public IP at:\n";
    std::cout << "  https://whatismyip.com\n";
    std::cout << "==========================================\n\n";

    char buffer[TELEMETRY_BUFFER_SIZE];
    sockaddr_in clientAddr{};
    int clientAddrLen = sizeof(clientAddr);
    
    uint32_t packetsReceived = 0;

    while (m_running)
    {
        int bytesReceived = recvfrom(
            serverSocket, 
            buffer, 
            TELEMETRY_BUFFER_SIZE - 1, 
            0,
            (sockaddr*)&clientAddr, 
#ifdef _WIN32
            &clientAddrLen
#else
            (socklen_t*)&clientAddrLen
#endif
        );

        if (bytesReceived > 0)
        {
            buffer[bytesReceived] = '\0';
            packetsReceived++;
            
            // Log received packet info
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            
            std::cout << "[Telemetry] #" << packetsReceived 
                      << " from " << clientIP << ":" << ntohs(clientAddr.sin_port)
                      << " (" << bytesReceived << " bytes)\n";
            
            if (ProcessJson(std::string(buffer)))
            {
                std::cout << "[Telemetry] OK\n";
            }
            else
            {
                std::cout << "[Telemetry] FAILED: " << buffer << "\n";
            }
        }
    }

    std::cout << "[TelemetryServer] Total packets: " << packetsReceived << "\n";
    closesocket(serverSocket);
#ifdef _WIN32
    WSACleanup();
#endif
}

bool TelemetryServer::ProcessJson(const std::string& jsonStr)
{
    TelemetryPacket packet;
    
    if (!ParseTelemetryJson(jsonStr, packet))
    {
        return false;
    }

    ParsedTelemetry telemetry = ConvertToReal(packet);
    
    // Log parsed data
    std::cout << "[Telemetry] ID=" << telemetry.deviceId 
              << " Lat=" << telemetry.latitude 
              << " Lon=" << telemetry.longitude 
              << " Fix=" << static_cast<int>(telemetry.fixType)
              << " Sats=" << static_cast<int>(telemetry.satellites) << "\n";

    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_callback)
    {
        m_callback(telemetry);
    }

    return true;
}

bool TelemetryServer::ParseTelemetryJson(const std::string& jsonStr, TelemetryPacket& packet)
{
    try
    {
        json j = json::parse(jsonStr);

        // Required fields
        packet.deviceId = j.value("id", "unknown");
        packet.lat = j.value("lat", 0);
        packet.lon = j.value("lon", 0);
        packet.time = j.value("time", 0u);

        // Optional fields with defaults
        packet.alt = j.value("alt", 0);
        packet.speed = j.value("speed", static_cast<uint16_t>(0));
        packet.course = j.value("course", static_cast<uint16_t>(0));
        packet.fixType = j.value("fixType", static_cast<uint8_t>(0));
        packet.sats = j.value("sats", static_cast<uint8_t>(0));
        packet.acc = j.value("acc", TELEMETRY_ACC_UNAVAILABLE);

        return true;
    }
    catch (const json::exception& e)
    {
        std::cerr << "[TelemetryServer] JSON error: " << e.what() << "\n";
        return false;
    }
}

ParsedTelemetry TelemetryServer::ConvertToReal(const TelemetryPacket& packet)
{
    ParsedTelemetry result;

    result.deviceId = packet.deviceId;
    result.latitude = static_cast<double>(packet.lat) * TELEMETRY_LAT_LON_SCALE;
    result.longitude = static_cast<double>(packet.lon) * TELEMETRY_LAT_LON_SCALE;
    result.altitude = static_cast<double>(packet.alt) / TELEMETRY_ALT_SCALE;
    result.timestamp = packet.time;
    result.speed = static_cast<double>(packet.speed) / TELEMETRY_SPEED_SCALE;
    result.course = static_cast<double>(packet.course) / TELEMETRY_COURSE_SCALE;
    result.fixType = static_cast<GpsFixType>(packet.fixType);
    result.satellites = packet.sats;

    if (packet.acc == TELEMETRY_ACC_UNAVAILABLE)
    {
        result.acceleration = 0.0;
        result.accelerationValid = false;
    }
    else
    {
        result.acceleration = static_cast<double>(packet.acc);
        result.accelerationValid = true;
    }

    return result;
}
