#pragma once
#include "../input/Input.h"
#include <vector>
#include <glm/glm.hpp>

struct SplinePoint {
    glm::vec2 position;
    glm::vec2 tangent; // Нормализованный вектор направления
};

glm::vec2 LerpDerivative(const glm::vec2& p0, const glm::vec2& p1,
    const glm::vec2& v0, const glm::vec2& v1,
    float t, float t0, float t1);

std::vector<SplinePoint> InterpolatePointsWithTangents(const std::vector<glm::vec2>& originalPoints, int pointsPerSegment, float alpha =0.5f);

std::vector<SplinePoint> InterpolateRoundedPolyline(const std::vector<glm::vec2>& points, float radius, int segmentsPerCorner);

std::vector<glm::vec2> SmoothPath(const std::vector<glm::vec2>& rawPoints, int windowSize = 3);

std::vector<glm::vec2> SimplifyPath(const std::vector<glm::vec2>& points, float tolerance);

std::vector<glm::vec2> FilterPointsByDistance(const std::vector<glm::vec2>& points, float minDistance);

std::vector<glm::vec2> GenerateTriangleStripFromLine(const std::vector<SplinePoint>& splinePoints, float width);
