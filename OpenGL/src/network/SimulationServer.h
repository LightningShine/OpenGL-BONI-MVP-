#pragma once

#include <vector>
#include "../rendering/Interpolation.h"
#include "../network/Server.h"

// Unified telemetry processing
void processIncomingTelemetry(const TelemetryPacket& packet);

// Telemetry counters (for UI)
// Returns the number of telemetry packets received during the last completed 1-second interval.
uint32_t telemetryGetPacketsPerSecond();

// Optional: reset counters when data source changes (e.g., COM port switched)
void telemetryResetPpsCounters();
void processIncomingVehicleState(const VehicleStatePacket& packet);

// Simulate vehicle movement along pre-interpolated track
void simulateVehicleMovement(int vehicle_id, const std::vector<SplinePoint>& smooth_track_points);
