// ============================================================================
// NetworkCompat — leftovers after the GameNetworkingSockets removal.
//
// Hosting/broadcasting moved to the standalone RAJAGP Track Server; this app
// is now a viewer (TrackServerClient) + standalone COM-port receiver. The
// symbols below keep the untouched call sites (serial reader, simulation)
// compiling; the broadcasts are intentionally no-ops.
// ============================================================================

#include "Server.h"

#include <atomic>

// Mode flags: the app no longer hosts a GNS server nor runs a GNS client, so
// both stay false; the serial reader checks them before broadcasting.
std::atomic<bool> g_is_server_mode{false};
std::atomic<bool> g_is_client_mode{false};

void BroadcastTelemetryToClients(const TelemetryPacket&)
{
    // no-op: fan-out to viewers is done by the RAJAGP Track Server
}

void BroadcastVehicleStateToClients(const VehicleStatePacket&)
{
    // no-op: fan-out to viewers is done by the RAJAGP Track Server
}
