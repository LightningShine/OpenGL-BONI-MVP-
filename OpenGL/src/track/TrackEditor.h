#pragma once

#include <vector>
#include <glm/glm.hpp>

// Geometry cleanup for recorded track edges.
// Points are in normalized map units (MapConstants::MAP_SIZE); every
// parameter here is in meters so the UI can show human-friendly values.
namespace TrackEditor
{
	// Removes GPS jitter (body sway on a bike/kart) while guaranteeing the
	// result never deviates from the recorded line by more than
	// toleranceMeters. Real geometry — slow drifts, corners — that is larger
	// than the tolerance always survives.
	std::vector<glm::vec2> SmoothEdge(const std::vector<glm::vec2>& rawPoints,
	                                  float toleranceMeters);

	// Welds the end of a recorded lap onto its start. The remaining gap is
	// spread over the last blendMeters of the line so the seam stays smooth.
	// Returns false (and leaves the points untouched) when the ends are
	// farther apart than maxGapMeters.
	bool CloseLoop(std::vector<glm::vec2>& points,
	               float maxGapMeters, float blendMeters);

	// Distance between the first and the last point, in meters.
	float EndGapMeters(const std::vector<glm::vec2>& points);

	float PolylineLengthMeters(const std::vector<glm::vec2>& points);

	// Track width for two aligned, equal-sized edges (min/max in meters).
	void MeasureWidth(const std::vector<glm::vec2>& leftEdge,
	                  const std::vector<glm::vec2>& rightEdge,
	                  float& outMinMeters, float& outMaxMeters);
}
