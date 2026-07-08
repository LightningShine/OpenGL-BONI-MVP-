#include "TrackServerClient.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "Server.h"             // TelemetryPacket (rajagp_core alias)
#include "SimulationServer.h"   // processIncomingTelemetry
#include "../vehicle/Vehicle.h" // g_vehicles authoritative timing update
#include "../input/Input.h"     // g_map_origin (map origin from the track frame)

#include <GeographicLib/UTMUPS.hpp>

namespace TrackServerClient {
namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
std::mutex  g_mutex;
std::string g_host = "127.0.0.1";
uint16_t    g_port = 8080;
std::string g_token;
std::string g_role;
std::string g_flag = "none";

std::atomic<bool> g_running{false};
std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_connected{false};
std::atomic<bool> g_failure{false};
std::thread g_thread;

// WebSocket handle shared with stop() so a blocking receive can be aborted.
std::mutex g_ws_mutex;
HINTERNET  g_ws = nullptr;

// Track geometry handoff (socket thread → render thread).
std::mutex g_track_mutex_local;
std::vector<glm::vec2> g_track_left, g_track_right;
bool g_track_pending = false;

// Map origin from the track frame — applied together with the geometry.
// Without it processIncomingTelemetry drops every packet ("map origin not
// initialized"), which is exactly what loadTrk2File() fills in locally.
struct PendingOrigin {
    double easting = 0.0, northing = 0.0, map_size = 100.0;
    int    zone = 0;
    char   zone_char = 'U';
    bool   valid = false;
};
PendingOrigin g_track_origin;

// Admin-panel data (socket thread writes, UI reads).
std::mutex g_admin_mutex;
std::vector<std::string> g_admin_responses;
std::vector<UserInfo> g_users;

// Race state from the server ("" until the first state frame).
std::string g_race_state;
uint32_t    g_race_epoch = 0;
bool        g_have_epoch = false;

// ---------------------------------------------------------------------------
// Link quality from state frames: loss via "seq" gaps, delay via the
// (arrival - server_time_ms) spread over a ~5 s sliding window.
// ---------------------------------------------------------------------------
struct FrameStat {
    uint32_t arrival_ms;   // local monotonic
    uint32_t lost_before;  // seq gap in front of this frame
    int64_t  clock_diff;   // arrival_ms - server_time_ms (offset unknown)
};
std::mutex g_stats_mutex;
std::deque<FrameStat> g_frame_stats;
uint32_t g_last_seq = 0;
bool     g_have_seq = false;

uint32_t monotonicMs()
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void recordFrameStat(uint32_t seq, uint32_t server_ms)
{
    const uint32_t now = monotonicMs();
    std::lock_guard<std::mutex> lock(g_stats_mutex);

    uint32_t lost = 0;
    if (g_have_seq && seq > g_last_seq + 1)
        lost = seq - g_last_seq - 1;
    // Always adopt (WebSocket is ordered; seq going BACK means server restart).
    g_last_seq = seq;
    g_have_seq = true;

    g_frame_stats.push_back({now, lost,
        static_cast<int64_t>(now) - static_cast<int64_t>(server_ms)});
    while (!g_frame_stats.empty() &&
           now - g_frame_stats.front().arrival_ms > 5000)
        g_frame_stats.pop_front();
}

void pushAdminResponse(std::string line)
{
    std::lock_guard<std::mutex> lock(g_admin_mutex);
    g_admin_responses.push_back(std::move(line));
    if (g_admin_responses.size() > 20)
        g_admin_responses.erase(g_admin_responses.begin());
}

// ---------------------------------------------------------------------------
// Tiny JSON field readers for the server's fixed, machine-generated frames.
// Not a general parser — good for flat numeric/string fields we produce.
// ---------------------------------------------------------------------------
std::string jsonString(const std::string& text, const std::string& key,
                       size_t from = 0)
{
    const std::string pat = "\"" + key + "\":\"";
    const auto pos = text.find(pat, from);
    if (pos == std::string::npos) return {};
    const auto start = pos + pat.size();
    const auto end = text.find('"', start);
    if (end == std::string::npos) return {};
    return text.substr(start, end - start);
}

double jsonNumber(const std::string& text, const std::string& key,
                  size_t from, bool& ok)
{
    const std::string pat = "\"" + key + "\":";
    const auto pos = text.find(pat, from);
    if (pos == std::string::npos) { ok = false; return 0.0; }
    ok = true;
    return std::strtod(text.c_str() + pos + pat.size(), nullptr);
}

// Parse "key":[[x,y],[x,y],...] into points.
bool jsonPointArray(const std::string& text, const std::string& key,
                    std::vector<glm::vec2>& out)
{
    const std::string pat = "\"" + key + "\":[";
    auto pos = text.find(pat);
    if (pos == std::string::npos) return false;
    pos += pat.size();
    while (pos < text.size() && text[pos] != ']') {
        if (text[pos] == '[') {
            const char* p = text.c_str() + pos + 1;
            char* endp = nullptr;
            const float x = std::strtof(p, &endp);
            if (endp && *endp == ',') {
                const float y = std::strtof(endp + 1, nullptr);
                out.emplace_back(x, y);
            }
            const auto close = text.find(']', pos);
            if (close == std::string::npos) return false;
            pos = close + 1;
        } else {
            ++pos;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Frame handlers
// ---------------------------------------------------------------------------
void handleState(const std::string& text)
{
    // PPS counts NETWORK PACKETS: one state frame = one packet, no matter how
    // many car records it carries.
    telemetryCountPacket();

    // Link quality: sequence gap = loss, server_time_ms spread = delay.
    {
        bool ok_seq = false, ok_ms = false;
        const auto seq = static_cast<uint32_t>(jsonNumber(text, "seq", 0, ok_seq));
        const auto sms = static_cast<uint32_t>(jsonNumber(text, "server_time_ms", 0, ok_ms));
        if (ok_seq && ok_ms)
            recordFrameStat(seq, sms);
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const std::string f = jsonString(text, "flag");
        if (!f.empty()) g_flag = f;
        const std::string rs = jsonString(text, "race");
        if (!rs.empty()) g_race_state = rs;
    }

    // New race epoch (admin pressed Start/Reset on the server): wipe local lap
    // history so the session counts from zero — practice data must not leak
    // into the race results.
    {
        bool ok = false;
        const auto epoch = static_cast<uint32_t>(jsonNumber(text, "epoch", 0, ok));
        if (ok && (!g_have_epoch || epoch != g_race_epoch)) {
            const bool is_new_session = g_have_epoch; // first frame just adopts
            g_race_epoch = epoch;
            g_have_epoch = true;
            if (is_new_session) {
                std::lock_guard<std::mutex> lock(g_vehicles_mutex);
                for (auto& [id, v] : g_vehicles) {
                    v.m_laps.clear();
                    v.laps.clear();
                    v.m_best_lap_time       = -1.0f;
                    v.m_completed_laps      = 0;
                    v.m_current_lap_number  = 1;
                    v.m_current_lap_timer   = 0.0f;
                    v.m_has_started_first_lap = false;
                    v.m_is_leader           = false;
                    v.m_server_position     = 0;
                    v.m_is_finished         = false;
                }
                std::cout << "[TRACK-CLIENT] race epoch " << epoch
                          << " — local lap history cleared" << std::endl;
            }
        }
    }

    const auto cars_pos = text.find("\"cars\":[");
    if (cars_pos == std::string::npos) return;

    size_t pos = cars_pos + 8;
    while (true) {
        const auto obj_start = text.find('{', pos);
        if (obj_start == std::string::npos) break;
        const auto obj_end = text.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        const std::string car = text.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;

        bool ok = false;
        const auto id = static_cast<int32_t>(jsonNumber(car, "id", 0, ok));
        if (!ok) continue;

        // Raw values → the existing telemetry pipeline (GPS→UTM, vehicle
        // creation/updates, interpolation) — same as local COM reception.
        TelemetryPacket pkt{};
        pkt.MagicMarker  = PACKET_MAGIC_DATA;
        pkt.ID           = id;
        pkt.lat          = static_cast<int32_t>(jsonNumber(car, "lat", 0, ok));
        pkt.lon          = static_cast<int32_t>(jsonNumber(car, "lon", 0, ok));
        pkt.time         = static_cast<uint32_t>(jsonNumber(car, "gps_ms", 0, ok));
        pkt.speed        = static_cast<uint32_t>(jsonNumber(car, "speed", 0, ok));
        pkt.acceleration = static_cast<uint32_t>(jsonNumber(car, "accel", 0, ok));
        pkt.gForceX      = static_cast<uint16_t>(jsonNumber(car, "gfx", 0, ok));
        pkt.gForceY      = static_cast<uint16_t>(jsonNumber(car, "gfy", 0, ok));
        pkt.fixtype      = static_cast<int16_t>(jsonNumber(car, "fix", 0, ok));
        processIncomingTelemetry(pkt, /*count_pps=*/false); // frame counted above

        // Server-computed timings → authoritative vehicle state (the server
        // computes, the client draws). Vehicles are stored under their RACE id
        // (1..99), not the device id — translate via the prototype mapping.
        const auto lap      = static_cast<int>(jsonNumber(car, "lap", 0, ok));
        const auto position = static_cast<int>(jsonNumber(car, "pos", 0, ok));
        const float best    = static_cast<float>(jsonNumber(car, "best_lap", 0, ok));
        const float lap_t   = static_cast<float>(jsonNumber(car, "lap_time", 0, ok));
        const float last_t  = static_cast<float>(jsonNumber(car, "last_lap", 0, ok));
        const bool finished = jsonNumber(car, "fin", 0, ok) != 0.0;
        const int32_t race_id = telemetryGetRaceIdForPrototype(id);
        if (race_id != -1) {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            auto it = g_vehicles.find(race_id);
            if (it != g_vehicles.end()) {
                Vehicle& v = it->second;
                v.m_has_authoritative_state = true;
                v.m_current_lap_number = lap;
                v.m_completed_laps     = lap > 0 ? lap - 1 : 0;
                v.m_current_lap_timer  = lap_t;
                if (best > 0.0f) v.m_best_lap_time = best;
                v.m_is_leader = (position == 1);
                v.m_has_started_first_lap = lap > 0;
                v.m_server_position = position;
                v.m_is_finished     = finished;
                // Server-computed completed-lap time → per-lap history, so the
                // PRO panels (previous lap / lap list) work in networked mode
                // where the local RaceManager does not detect laps itself.
                if (last_t > 0.0f && lap >= 2) {
                    LapData& rec = v.m_laps[lap - 1];
                    if (rec.lapTime <= 0.0f) {
                        rec.lapTime = last_t;
                        rec.positionAtFinish = position;
                    }
                }
            }
        }
    }
}

void handleMessage(const std::string& text)
{
    const std::string type = jsonString(text, "type");
    if (type == "state") {
        handleState(text);
    } else if (type == "hello") {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_role = jsonString(text, "role");
        g_connected.store(true);
        std::cout << "[TRACK-CLIENT] connected, role=" << g_role << std::endl;
    } else if (type == "track") {
        std::vector<glm::vec2> left, right;
        if (jsonPointArray(text, "left", left) &&
            jsonPointArray(text, "right", right) && !left.empty() && !right.empty()) {
            bool ok = false;
            PendingOrigin origin;
            origin.easting  = jsonNumber(text, "origin_easting", 0, ok);
            origin.valid    = ok;
            origin.northing = jsonNumber(text, "origin_northing", 0, ok);
            origin.zone     = static_cast<int>(jsonNumber(text, "origin_zone", 0, ok));
            origin.map_size = jsonNumber(text, "map_size", 0, ok);
            const std::string zc = jsonString(text, "origin_zone_char");
            if (!zc.empty()) origin.zone_char = zc[0];

            std::lock_guard<std::mutex> lock(g_track_mutex_local);
            g_track_left = std::move(left);
            g_track_right = std::move(right);
            g_track_origin = origin;
            g_track_pending = true;
            std::cout << "[TRACK-CLIENT] track received: left=" << g_track_left.size()
                      << " right=" << g_track_right.size()
                      << " origin_valid=" << origin.valid << std::endl;
        }
    }
    else if (type == "user_created") {
        pushAdminResponse("User '" + jsonString(text, "name") +
                          "' created. TOKEN: " + jsonString(text, "token") +
                          "  (expires: " + jsonString(text, "expires_at") + ")");
    } else if (type == "user_revoked") {
        pushAdminResponse("User '" + jsonString(text, "name") + "' revoked.");
    } else if (type == "error") {
        pushAdminResponse("Server error: " + jsonString(text, "error"));
    } else if (type == "users") {
        // {"type":"users","users":[{"name":"x","expires_at":"...","expired":false},...]}
        std::vector<UserInfo> users;
        const auto arr = text.find("\"users\":[");
        size_t pos = (arr == std::string::npos) ? std::string::npos : arr + 9;
        while (pos != std::string::npos) {
            const auto obj_start = text.find('{', pos);
            if (obj_start == std::string::npos) break;
            const auto obj_end = text.find('}', obj_start);
            if (obj_end == std::string::npos) break;
            const std::string obj = text.substr(obj_start, obj_end - obj_start + 1);
            pos = obj_end + 1;
            UserInfo u;
            u.name = jsonString(obj, "name");
            u.expires = jsonString(obj, "expires_at");
            u.token = jsonString(obj, "token");
            if (obj.find("\"expired\":true") != std::string::npos)
                u.expires += " (expired)";
            if (!u.name.empty())
                users.push_back(std::move(u));
        }
        std::lock_guard<std::mutex> lock(g_admin_mutex);
        g_users = std::move(users);
    }
    // history frames need no client-side action yet.
}

// ---------------------------------------------------------------------------
// Connection loop
// ---------------------------------------------------------------------------
std::wstring widen(const std::string& s)
{
    return std::wstring(s.begin(), s.end()); // host/path are ASCII
}

// One connect + receive session. Returns when the socket drops or stop is set.
void runOnce(const std::string& host, uint16_t port, const std::string& token)
{
    HINTERNET session = WinHttpOpen(L"RAJAGP-Client/1.0",
                                    WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { g_failure.store(true); return; }

    HINTERNET connect = WinHttpConnect(session, widen(host).c_str(), port, 0);
    HINTERNET request = nullptr;
    HINTERNET ws = nullptr;

    do {
        if (!connect) break;
        std::string path = "/";
        if (!token.empty()) path += "?token=" + token;
        request = WinHttpOpenRequest(connect, L"GET", widen(path).c_str(),
                                     nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!request) break;

        if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                              nullptr, 0))
            break;
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                nullptr, 0, 0, 0))
            break;
        if (!WinHttpReceiveResponse(request, nullptr))
            break;

        ws = WinHttpWebSocketCompleteUpgrade(request, 0);
    } while (false);

    if (request) { WinHttpCloseHandle(request); request = nullptr; }

    if (!ws) {
        std::cout << "[TRACK-CLIENT] connect failed  host=" << host
                  << " port=" << port << " err=" << GetLastError() << std::endl;
        g_failure.store(true);
        if (connect) WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        g_ws = ws;
    }
    g_failure.store(false);

    // Receive loop: reassemble fragmented UTF-8 messages.
    std::string message;
    std::vector<char> buf(64 * 1024);
    while (!g_stop_requested.load()) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        const DWORD rc = WinHttpWebSocketReceive(
            ws, buf.data(), static_cast<DWORD>(buf.size()), &read, &type);
        if (rc != NO_ERROR)
            break;
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
            break;
        message.append(buf.data(), read);
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            handleMessage(message);
            message.clear();
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        g_ws = nullptr;
    }
    g_connected.store(false);
    WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    WinHttpCloseHandle(ws);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    std::cout << "[TRACK-CLIENT] disconnected" << std::endl;
}

void runLoop()
{
    while (!g_stop_requested.load()) {
        std::string host, token;
        uint16_t port;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            host = g_host;
            port = g_port;
            token = g_token;
        }
        runOnce(host, port, token);
        // Reconnect with backoff unless the app is shutting down.
        for (int i = 0; i < 30 && !g_stop_requested.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    g_running.store(false);
}

} // namespace

void setConnectParams(const std::string& host, uint16_t port,
                      const std::string& token)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_host = host;
    g_port = port != 0 ? port : 8080;
    g_token = token;
}

void start()
{
    if (g_running.exchange(true))
        return; // already running
    g_stop_requested.store(false);
    g_failure.store(false);
    if (g_thread.joinable())
        g_thread.join();
    g_thread = std::thread(runLoop);
}

void stop()
{
    g_stop_requested.store(true);
    {
        // Abort a blocking receive so the thread can exit promptly.
        std::lock_guard<std::mutex> lock(g_ws_mutex);
        if (g_ws)
            WinHttpWebSocketClose(g_ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                                  nullptr, 0);
    }
    if (g_thread.joinable())
        g_thread.join();
    g_running.store(false);
    g_connected.store(false);
}

bool isRunning()   { return g_running.load(); }
bool isConnected() { return g_connected.load(); }
bool hadFailure()  { return g_failure.load(); }
void clearFailure(){ g_failure.store(false); }

std::string role()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_role;
}

std::string currentFlag()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_flag;
}

std::string raceState()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_race_state;
}

float netLossPercent()
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    if (g_frame_stats.empty())
        return 0.0f;
    uint64_t lost = 0;
    for (const auto& s : g_frame_stats)
        lost += s.lost_before;
    const uint64_t expected = lost + g_frame_stats.size();
    return expected ? 100.0f * static_cast<float>(lost) / expected : 0.0f;
}

int netDelayMs()
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    if (g_frame_stats.size() < 2)
        return 0;
    // Clocks are not synchronized, so the absolute offset is unknown — but the
    // MINIMUM (arrival - server_time) over the window is the "fast path"
    // baseline; the average above it is queuing/jitter delay.
    int64_t min_diff = g_frame_stats.front().clock_diff;
    for (const auto& s : g_frame_stats)
        min_diff = s.clock_diff < min_diff ? s.clock_diff : min_diff;
    int64_t sum = 0;
    for (const auto& s : g_frame_stats)
        sum += s.clock_diff - min_diff;
    return static_cast<int>(sum / static_cast<int64_t>(g_frame_stats.size()));
}

bool sendCommand(const std::string& json)
{
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    if (!g_ws)
        return false;
    const DWORD rc = WinHttpWebSocketSend(
        g_ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(json.data()), static_cast<DWORD>(json.size()));
    return rc == NO_ERROR;
}

std::vector<std::string> adminResponses()
{
    std::lock_guard<std::mutex> lock(g_admin_mutex);
    return g_admin_responses;
}

void clearResponses()
{
    std::lock_guard<std::mutex> lock(g_admin_mutex);
    g_admin_responses.clear();
}

std::vector<UserInfo> userList()
{
    std::lock_guard<std::mutex> lock(g_admin_mutex);
    return g_users;
}

bool consumePendingTrack(std::vector<glm::vec2>& left,
                         std::vector<glm::vec2>& right)
{
    PendingOrigin origin;
    {
        std::lock_guard<std::mutex> lock(g_track_mutex_local);
        if (!g_track_pending)
            return false;
        left = std::move(g_track_left);
        right = std::move(g_track_right);
        origin = g_track_origin;
        g_track_left.clear();
        g_track_right.clear();
        g_track_pending = false;
    }

    // Apply the map origin exactly like loadTrk2File() does for a local file —
    // this is what lets processIncomingTelemetry convert GPS→map coordinates.
    if (origin.valid) {
        g_map_origin.m_origin_meters_easting  = origin.easting;
        g_map_origin.m_origin_meters_northing = origin.northing;
        g_map_origin.m_origin_zone_int        = origin.zone;
        g_map_origin.m_origin_zone_char       = origin.zone_char;
        g_map_origin.m_map_size               = origin.map_size;
        try {
            const bool northp = origin.zone_char >= 'N';
            GeographicLib::UTMUPS::Reverse(origin.zone, northp,
                origin.easting, origin.northing,
                g_map_origin.m_origin_lat_dd, g_map_origin.m_origin_lon_dd);
        } catch (...) {}
        g_is_map_loaded = true;
        std::cout << "[TRACK-CLIENT] map origin applied: zone="
                  << origin.zone << origin.zone_char
                  << " E=" << origin.easting << " N=" << origin.northing << std::endl;
    }
    return true;
}

} // namespace TrackServerClient
