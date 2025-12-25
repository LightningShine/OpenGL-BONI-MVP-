#pragma once
#include "../input/Input.h"
#include <vector>
#include <glm/glm.hpp>

struct SplinePoint {
    glm::vec2 position;
    glm::vec2 tangent; // ??????????????? ?????? ???????????
};

// Interpolates a set of points using Centripetal Catmull-Rom Spline algorithm.
// originalPoints: The raw input coordinates.
// pointsPerSegment: How many new points to generate between two original points.
// alpha: 0.0 = Uniform (default), 0.5 = Centripetal (avoids loops), 1.0 = Chordal.
//std::vector<glm::vec2> InterpolatePoints(const std::vector<glm::vec2>& originalPoints, int pointsPerSegment, float alpha = 0.5f);

glm::vec2 LerpDerivative(const glm::vec2& p0, const glm::vec2& p1,
    const glm::vec2& v0, const glm::vec2& v1,
    float t, float t0, float t1);

std::vector<SplinePoint> InterpolatePointsWithTangents(const std::vector<glm::vec2>& originalPoints, int pointsPerSegment, float alpha = 0.5f);

// Generates triangle strip vertices from a line path with specified width
// linePoints: The center line points
// width: Half-width of the path (distance from center to edge)
// Returns: Vector of vertices for GL_TRIANGLE_STRIP rendering
std::vector<glm::vec2> GenerateTriangleStripFromLine(const std::vector<SplinePoint>& splinePoints, float width);
