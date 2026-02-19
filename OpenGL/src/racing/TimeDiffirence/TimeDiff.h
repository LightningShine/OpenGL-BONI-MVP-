#pragma once
#include "../vehicle/Vehicle.h"

// ============================================================================
// TIME DIFFERENCE CALCULATION FUNCTIONS
// ============================================================================

// Calculate time difference between current lap and best lap (with interpolation)
float CalculateLapTimeDiff(int vehicleID);          // Thread-safe (with mutex)
float CalculateLapTimeDiffInternal(int vehicleID);  // Internal (no mutex)

// Calculate time difference to race leader at same total progress
float CalculateLeaderTimeDiff(int vehicleID);          // Thread-safe (with mutex)
float CalculateLeaderTimeDiffInternal(int vehicleID);  // Internal (no mutex)
