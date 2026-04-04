#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct TelemetryPacket;
struct SplinePoint;
class MapOrigin;

namespace TelemetryTrackBuilder
{
	struct Settings
	{
		float minPointDistanceNorm = 0.0015f;
		float closeRadiusNorm = 0.01f;
		size_t minPointsToClose = 200;
		float minLoopLengthMeters = 80.0f;
		int splinePointsPerSegment = 6;
	};

	void Start(const Settings& settings);
	void Stop();
    void Stop(bool keepPoints);
	bool IsActive();
	bool IsFinalized();
	bool ConsumeAutoSaveRequest();

	void OnTelemetryPacket(const TelemetryPacket& packet);

	// Finalize as open track (does not require closure) and save to .txt.
	// userNameOrPath: if empty, a default name is used. If has no extension, .txt is appended.
	bool FinalizeOpenAndSaveTxt(const std::string& userNameOrPath);
	bool SaveFinalizedAsTxt(const std::string& userNameOrPath);

	std::vector<glm::vec2> GetRawPointsSnapshot();
	std::vector<SplinePoint> GetSmoothPointsSnapshot();
	glm::vec2 GetStartFinishP1();
	glm::vec2 GetStartFinishP2();
}
