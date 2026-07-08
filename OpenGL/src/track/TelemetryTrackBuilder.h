#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// TelemetryPacket now lives in rajagp_core; the old local forward declaration
// would clash with the alias in Server.h, so pull in the real definition.
#include <rajagp/Protocol.h>
using TelemetryPacket = rajagp::TelemetryPacket;

struct SplinePoint;
class MapOrigin;

enum class EdgePhase { Idle, Left, Right, Done };

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

	// ── Single-pass (centre-line) mode ─────────────────────────────────────
	void Start(const Settings& settings = Settings{});
	void Stop();
	void Stop(bool keepPoints);
	bool IsActive();
	bool IsFinalized();
	bool ConsumeAutoSaveRequest();
	void OnTelemetryPacket(const TelemetryPacket& packet);
	bool FinalizeOpenAndSaveTxt(const std::string& userNameOrPath);
	bool SaveFinalizedAsTxt(const std::string& userNameOrPath);

	// ── Dual-edge mode ──────────────────────────────────────────────────────
	void StartLeftEdge(const Settings& settings = Settings{});
	bool SwitchToRightEdge();   // false if left edge has too few points
	bool FinalizeEdges();       // false if right edge has too few points

	EdgePhase GetPhase();
	std::vector<glm::vec2> GetLeftEdgeSnapshot();  // stored left edge
	// GetRawPointsSnapshot() returns the active recording (left during Left, right during Right)

	// Feed synthetic edges directly (no GPS needed — for testing)
	bool InjectSyntheticEdges(std::vector<glm::vec2> left, std::vector<glm::vec2> right);

	// ── Snapshots ───────────────────────────────────────────────────────────
	std::vector<glm::vec2>  GetRawPointsSnapshot();
	std::vector<SplinePoint> GetSmoothPointsSnapshot();
	glm::vec2 GetStartFinishP1();
	glm::vec2 GetStartFinishP2();
}
