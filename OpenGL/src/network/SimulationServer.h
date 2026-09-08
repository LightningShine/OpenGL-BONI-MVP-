#pragma once

#include "network/Server.h"

// Unified telemetry processing entry points implemented in `SimulationServer.cpp`.
// (Used by real COM capture, simulation and network.)
// count_pps=false lets a caller that receives MULTIPLE car records in one
// network packet (Track Server state frame) count the packet itself instead.
void processIncomingTelemetry(const TelemetryPacket& packet, bool count_pps = true);

/// Находится ли точка (в нормализованных координатах трека) ближе
/// `radius_meters` к загруженной трассе. false, если трассы нет.
/// Один и тот же критерий «наша машина» для приёма телеметрии и для проверки,
/// та ли трасса открыта под запись повтора.
bool positionIsNearLoadedTrack(double normalized_x, double normalized_y, double radius_meters);

/// Широта/долгота -> нормализованные координаты трека (через UTM и origin карты).
void normalizedFromGps(double lat_deg, double lon_deg, double& out_x, double& out_y);
void processIncomingVehicleState(const VehicleStatePacket& packet);

// Count one received packet in the PPS window (used with count_pps=false).
void telemetryCountPacket();

// Telemetry diagnostics helpers
uint32_t telemetryGetPacketsPerSecond();
void telemetryResetPpsCounters();
void telemetryResetPrototypeIdMapping();

#include <vector>
#include "rendering/Interpolation.h"
#include "network/Server.h"

// Unified telemetry processing
void processIncomingTelemetry(const TelemetryPacket& packet, bool count_pps);

// Telemetry counters (for UI)
// Returns the number of telemetry packets received during the last completed 1-second interval.
uint32_t telemetryGetPacketsPerSecond();

// Optional: reset counters when data source changes (e.g., COM port switched)
void telemetryResetPpsCounters();

// Resets prototype->race vehicle ID mapping (used when changing data sources).
void telemetryResetPrototypeIdMapping();

// Releases the prototype->race mapping entries pointing at a race vehicle ID.
// Принимает именно race ID (номер участника, он же ключ g_vehicles), не ID железки.
void telemetryReleaseRaceIdMapping(int32_t raceID);

// Снимает сглаживающее состояние: буферы интерполятора, тайм-синк, сглаживание
// таймеров, лимитеры отправки. Привязки устройств НЕ трогает — они действуют на
// всю сессию, и сбрасывать их при переходе на ключевой кадр нельзя: устройство
// получило бы новый номер при живой машине со старым.
void telemetryResetInterpolationState();

// То же плюс привязки устройств. Нужно перед полным пересчётом с нуля, когда
// машин не остаётся вовсе.
void telemetryResetAllVehicleState();

// Единая точка «машины с этим race ID больше нет»: снимает привязку устройства,
// буфер интерполяции, тайм-синк, сглаживание таймера и лимитер репликации.
// Вызывать после удаления из g_vehicles и БЕЗ удерживаемого g_vehicles_mutex.
void telemetryForgetVehicle(int32_t race_id);

// Race vehicle ID assigned to a hardware/prototype device ID (-1 if the
// device has not been seen yet). Used by TrackServerClient to write the
// server-computed timings onto the right Vehicle in g_vehicles.
int32_t telemetryGetRaceIdForPrototype(int32_t prototype_id);

void processIncomingVehicleState(const VehicleStatePacket& packet);

// Simulate vehicle movement along pre-interpolated track
void simulateVehicleMovement(int vehicle_id, const std::vector<SplinePoint>& smooth_track_points);
void simulationStopAll();
