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

// Dual-edge recording walks through these phases:
//   Idle -> Left -> Transit -> Right -> Review -> Done
//   Left / Right — points of that edge are being recorded
//   Transit      — the rider drives from the left edge to the right edge
//                  start; NO points are recorded
//   Review       — both edges captured; the review screen cleans/edits them
//   Done         — reviewed track is finalized and goes to the save dialog
enum class EdgePhase { Idle, Left, Transit, Right, Review, Done };

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
	// Phase flow: StartLeftEdge → FinishLeftEdge → StartRightEdge →
	//             FinishRightEdge → (review screen) → SubmitReviewedEdges
	void StartLeftEdge(const Settings& settings = Settings{});
	bool FinishLeftEdge();      // Left → Transit  (false: too few points)
	bool StartRightEdge();      // Transit → Right
	bool FinishRightEdge();     // Right → Review  (false: too few points)

	// The review screen hands the cleaned/edited edges back. The builder
	// resamples them, rebuilds the centre line and requests the save dialog.
	bool SubmitReviewedEdges(std::vector<glm::vec2> left, std::vector<glm::vec2> right);

	EdgePhase GetPhase();
	std::vector<glm::vec2> GetLeftEdgeSnapshot();  // stored left edge
	// GetRawPointsSnapshot() returns the active recording (left during Left, right during Right)

	// ── Recording HUD info ──────────────────────────────────────────────────
	size_t PointCount();    // points in the edge being recorded right now
	float  LengthMeters();  // length of the edge being recorded right now
	int    LastFixType();   // GPS fix type from the latest packet (4+ = RTK)

	// Feed synthetic edges directly (no GPS needed — for testing)
	bool InjectSyntheticEdges(std::vector<glm::vec2> left, std::vector<glm::vec2> right);

	// ── Snapshots ───────────────────────────────────────────────────────────
	std::vector<glm::vec2>  GetRawPointsSnapshot();
	std::vector<SplinePoint> GetSmoothPointsSnapshot();
	glm::vec2 GetStartFinishP1();
	glm::vec2 GetStartFinishP2();
}
