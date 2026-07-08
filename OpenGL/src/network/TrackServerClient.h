#pragma once

// ============================================================================
// TrackServerClient — connects the RAJAGP app to the RAJAGP Track Server over
// WebSocket (replaces the removed GameNetworkingSockets client).
//
// Implemented on native WinHTTP (no third-party sockets), so it builds on
// Windows x64 AND ARM64 — the old GNS path was x64-only.
//
// What it consumes from the server:
//   * hello frame  → role (user/admin);
//   * track frame  → full track geometry, applied on the render thread via
//                    consumePendingTrack() (GPU upload must not happen here);
//   * state frames → every car is fed into the existing telemetry pipeline
//                    (processIncomingTelemetry) + server-computed timings
//                    (position/lap/best) are written into g_vehicles as
//                    authoritative values.
//
// The local serial (COM/SX1280) reception path is untouched — the app can
// still catch trackers by itself without any server.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace TrackServerClient {

// host like "192.168.1.10", port 8080, token may be empty (open server).
void setConnectParams(const std::string& host, uint16_t port,
                      const std::string& token);

// Spawns the connection thread (no-op if already running). Reconnects with a
// 3 s backoff until stop() is called.
void start();
void stop();

bool isRunning();
bool isConnected();   // WebSocket established + hello received
bool hadFailure();    // last connect attempt failed (shown red in the UI)
void clearFailure();

// "user" / "admin" (empty until hello arrives).
std::string role();

// Current race flag from the server: none/green/yellow/red/finish.
std::string currentFlag();

// Race state from the server: idle/running/finishing/finished ("" until the
// first state frame arrives).
std::string raceState();

// ---------------------------------------------------------------------------
// Link quality (computed from the state frames' seq + server_time_ms)
// ---------------------------------------------------------------------------
// Lost state frames over the last ~5 s, percent (0..100).
float netLossPercent();
// Extra network delay above the observed baseline, ms (jitter/queuing; the
// absolute one-way latency is unknowable without clock sync).
int netDelayMs();

// Render-thread handoff of the track geometry received on the socket thread.
// Returns true once per received track; caller uploads it to the GPU (the map
// origin from the frame is applied here too).
bool consumePendingTrack(std::vector<glm::vec2>& left,
                         std::vector<glm::vec2>& right);

// ---------------------------------------------------------------------------
// Admin channel (see server README: flag / visibility / create_user / ...)
// ---------------------------------------------------------------------------

// Send a raw JSON command to the server. False if not connected.
bool sendCommand(const std::string& json);

// Human-readable responses (user_created / errors / ...) for the admin panel,
// newest last, capped. Cleared with clearResponses().
std::vector<std::string> adminResponses();
void clearResponses();

// Users known to the server (refreshed via {"type":"list_users"}).
struct UserInfo {
    std::string name;
    std::string expires; // "never" or timestamp; "(expired)" appended if past
    std::string token;   // access token (admin connection only) — copyable
};
std::vector<UserInfo> userList();

} // namespace TrackServerClient
