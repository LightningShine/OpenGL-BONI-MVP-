#pragma once

#include "../network/Server.h"

// Unified telemetry processing entry points implemented in `SimulationServer.cpp`.
// (Used by real COM capture, simulation and network.)
void processIncomingTelemetry(const TelemetryPacket& packet);
void processIncomingVehicleState(const VehicleStatePacket& packet);

// Telemetry diagnostics helpers
uint32_t telemetryGetPacketsPerSecond();
void telemetryResetPpsCounters();
void telemetryResetPrototypeIdMapping();

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

// Resets prototype->race vehicle ID mapping (used when changing data sources).
void telemetryResetPrototypeIdMapping();
void processIncomingVehicleState(const VehicleStatePacket& packet);

// Simulate vehicle movement along pre-interpolated track
void simulateVehicleMovement(int vehicle_id, const std::vector<SplinePoint>& smooth_track_points);
void simulationStopAll();
