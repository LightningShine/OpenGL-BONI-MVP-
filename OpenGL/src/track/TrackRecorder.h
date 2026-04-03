#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct SplinePoint;
class MapOrigin;

namespace TrackRecorder
{
	struct Settings
	{
		float minPointDistanceNorm = 0.001f;
		float closeRadiusNorm = 0.01f;
		size_t minPointsToClose = 200;
		float minLoopLengthMeters = 50.0f;
		int pointsPerSegment = 6;
	};

	struct State
	{
		bool isRecording = false;
		bool isFinalized = false;
		int32_t sourceVehicleId = -1;
		glm::vec2 startPoint{ 0.0f, 0.0f };
		glm::vec2 lastPoint{ 0.0f, 0.0f };
		float approximateLengthMeters = 0.0f;
		size_t rawPointCount = 0;
	};

	void Start(int32_t vehicleId, const Settings& settings);
	void Stop();
	bool IsRecording();
	bool IsFinalized();
	State GetState();

	void OnTelemetryPosition(int32_t vehicleId, const glm::vec2& pos);
	std::vector<SplinePoint> GetSmoothTrackSnapshot();
	bool FinalizeNow();

	bool SaveToFile(const std::string& filename, const MapOrigin& origin);
	bool LoadFromFile(const std::string& filename, MapOrigin& out_origin, std::vector<SplinePoint>& out_track,
		glm::vec2& out_sf_p1, glm::vec2& out_sf_p2);
}
