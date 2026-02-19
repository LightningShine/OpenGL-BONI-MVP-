#pragma once
#include "../vehicle/Vehicle.h"

struct TimeDiff
{
	double track_progress;
	float time_from_start;
};

float CalculateTimeDiff(int vehicleID)
{
	// 1. Get best lap time from vehicle by ID
	// 2. Get current lap time from vehicle by ID
	// 3. Calculate the difference between current lap time and best lap time
	// 3.1 Find curent track progress from vehicle by ID
	// 3.2 Compare current track progress with the track progress at the best lap time
	// 4. Return the time difference


};