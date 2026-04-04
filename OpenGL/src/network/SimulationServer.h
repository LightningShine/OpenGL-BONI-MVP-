#pragma once

#include <vector>
#include "../rendering/Interpolation.h"
#include "../network/Server.h"

// Unified telemetry processing
void processIncomingTelemetry(const TelemetryPacket& packet);
void processIncomingVehicleState(const VehicleStatePacket& packet);

// Simulate vehicle movement along pre-interpolated track
void simulateVehicleMovement(int vehicle_id, const std::vector<SplinePoint>& smooth_track_points);
