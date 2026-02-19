#pragma once
#include "../vehicle/Vehicle.h"

struct TimeDiff
{
	double track_progress;
	float time_from_start;
};

float CalculateTimeDiff(int vehicleID);
