#include "TrackEditor.h"

#include "../Config.h"
#include "../rendering/Interpolation.h"

namespace
{
	float metersToNorm(float meters) { return meters / (float)MapConstants::MAP_SIZE; }
	float normToMeters(float norm)   { return norm * (float)MapConstants::MAP_SIZE; }
}

namespace TrackEditor
{
	std::vector<glm::vec2> SmoothEdge(const std::vector<glm::vec2>& rawPoints,
	                                  float toleranceMeters)
	{
		if (rawPoints.size() < 3)
			return rawPoints;

		// 1. Douglas-Peucker keeps only the anchor points that matter.
		//    Everything the tolerance tube can swallow (jitter) is dropped,
		//    anything bigger (a corner, a slow drift) is kept.
		std::vector<glm::vec2> anchors = simplifyPath(rawPoints, metersToNorm(toleranceMeters));
		if (anchors.size() < 3)
			return anchors;

		// 2. A Catmull-Rom spline through the anchors turns the angular
		//    simplified line back into a smooth one.
		std::vector<SplinePoint> spline = interpolatePointsWithTangents(anchors, 6);
		std::vector<glm::vec2> result;
		result.reserve(spline.size());
		for (const SplinePoint& sp : spline)
			result.push_back(sp.position);
		return result;
	}

	bool CloseLoop(std::vector<glm::vec2>& points,
	               float maxGapMeters, float blendMeters)
	{
		if (points.size() < 3)
			return false;

		const glm::vec2 gap = points.front() - points.back();
		if (normToMeters(glm::length(gap)) > maxGapMeters)
			return false;

		// Walk backwards from the end. Each point takes a share of the gap
		// that fades to zero blendMeters away, so the weld leaves no kink.
		const float blendNorm = metersToNorm(blendMeters);
		float distFromEnd = 0.0f;
		for (size_t i = points.size(); i-- > 0; )
		{
			if (i + 1 < points.size())
				distFromEnd += glm::distance(points[i], points[i + 1]);
			if (distFromEnd >= blendNorm)
				break;
			const float weight = 1.0f - distFromEnd / blendNorm;
			points[i] += gap * weight;
		}
		return true;
	}

	float EndGapMeters(const std::vector<glm::vec2>& points)
	{
		if (points.size() < 2)
			return 0.0f;
		return normToMeters(glm::distance(points.front(), points.back()));
	}

	float PolylineLengthMeters(const std::vector<glm::vec2>& points)
	{
		float lengthNorm = 0.0f;
		for (size_t i = 1; i < points.size(); ++i)
			lengthNorm += glm::distance(points[i], points[i - 1]);
		return normToMeters(lengthNorm);
	}

	void MeasureWidth(const std::vector<glm::vec2>& leftEdge,
	                  const std::vector<glm::vec2>& rightEdge,
	                  float& outMinMeters, float& outMaxMeters)
	{
		outMinMeters = 0.0f;
		outMaxMeters = 0.0f;
		const size_t count = leftEdge.size() < rightEdge.size() ? leftEdge.size() : rightEdge.size();
		if (count == 0)
			return;

		outMinMeters = 1e9f;
		for (size_t i = 0; i < count; ++i)
		{
			const float width = normToMeters(glm::distance(leftEdge[i], rightEdge[i]));
			if (width < outMinMeters) outMinMeters = width;
			if (width > outMaxMeters) outMaxMeters = width;
		}
	}
}
