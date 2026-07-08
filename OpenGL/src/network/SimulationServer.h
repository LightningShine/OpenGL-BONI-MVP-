#pragma once

#include "../network/Server.h"

// Unified telemetry processing entry points implemented in `SimulationServer.cpp`.
// (Used by real COM capture, simulation and network.)
// count_pps=false lets a caller that receives MULTIPLE car records in one
// network packet (Track Server state frame) count the packet itself instead.
void processIncomingTelemetry(const TelemetryPacket& packet, bool count_pps = true);
void processIncomingVehicleState(const VehicleStatePacket& packet);

// Count one received packet in the PPS window (used with count_pps=false).
void telemetryCountPacket();

// Telemetry diagnostics helpers
uint32_t telemetryGetPacketsPerSecond();
void telemetryResetPpsCounters();
void telemetryResetPrototypeIdMapping();

#include <vector>
#include "../rendering/Interpolation.h"
#include "../network/Server.h"

// Unified telemetry processing
void processIncomingTelemetry(const TelemetryPacket& packet, bool count_pps);

// Telemetry counters (for UI)
// Returns the number of telemetry packets received during the last completed 1-second interval.
uint32_t telemetryGetPacketsPerSecond();

// Optional: reset counters when data source changes (e.g., COM port switched)
void telemetryResetPpsCounters();

// Resets prototype->race vehicle ID mapping (used when changing data sources).
void telemetryResetPrototypeIdMapping();

// Race vehicle ID assigned to a hardware/prototype device ID (-1 if the
// device has not been seen yet). Used by TrackServerClient to write the
// server-computed timings onto the right Vehicle in g_vehicles.
int32_t telemetryGetRaceIdForPrototype(int32_t prototype_id);

void processIncomingVehicleState(const VehicleStatePacket& packet);

// Simulate vehicle movement along pre-interpolated track
void simulateVehicleMovement(int vehicle_id, const std::vector<SplinePoint>& smooth_track_points);
void simulationStopAll();
