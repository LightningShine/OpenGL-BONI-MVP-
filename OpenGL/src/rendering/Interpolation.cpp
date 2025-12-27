#include "Interpolation.h"
#include <cmath>
#include <iostream>
#include <algorithm>

// Structure to store the interpolation result


// Helper: Calculates the time knot (unchanged)
float GetT(float t, const glm::vec2& p0, const glm::vec2& p1, float alpha)
{
    float a = std::pow((p1.x - p0.x), 2.0f) + std::pow((p1.y - p0.y), 2.0f);
    float b = std::pow(a, 0.5f);
    float c = std::pow(b, alpha);
    if (c < 1e-4f) c = 1.0f;
    return (c + t);
}

// Helper: Standard Linear Interpolation (unchanged)
glm::vec2 Lerp(const glm::vec2& p0, const glm::vec2& p1, float t, float t0, float t1)
{
    if (std::abs(t1 - t0) < 1e-5f) return p0;
    return (p0 * (t1 - t) + p1 * (t - t0)) / (t1 - t0);
}

// NEW FUNCTION: Calculates derivative (velocity) for Lerp
// v0, v1 - velocities (derivatives) at points p0 and p1
glm::vec2 LerpDerivative(const glm::vec2& p0, const glm::vec2& p1,
    const glm::vec2& v0, const glm::vec2& v1,
    float t, float t0, float t1)
{
    float dt = t1 - t0;
    if (std::abs(dt) < 1e-5f) return glm::vec2(0.0f);
    // Derivative formula for recursive Lerp
    return ((p1 - p0) + v0 * (t1 - t) + v1 * (t - t0)) / dt;
}




// Change return type to std::vector<SplinePoint>
std::vector<SplinePoint> InterpolatePointsWithTangents(const std::vector<glm::vec2>& originalPoints, int pointsPerSegment, float alpha)
{
    std::vector<SplinePoint> resultPath;
    if (originalPoints.size() < 2) return resultPath;

    for (size_t i = 0; i < originalPoints.size() - 1; i++)
    {
        glm::vec2 p0, p1, p2, p3;
        p1 = originalPoints[i];
        p2 = originalPoints[i + 1];

        if (i == 0) p0 = p1 - (p2 - p1); else p0 = originalPoints[i - 1];
        if (i + 2 < originalPoints.size()) p3 = originalPoints[i + 2]; else p3 = p2 + (p2 - p1);

        float t0 = 0.0f;
        float t1 = GetT(t0, p0, p1, alpha);
        float t2 = GetT(t1, p1, p2, alpha);
        float t3 = GetT(t2, p2, p3, alpha);

        for (int j = 0; j < pointsPerSegment; j++)
        {
            float t = t1 + ((t2 - t1) * ((float)j / (float)pointsPerSegment));

            // --- Position (as before) ---
            glm::vec2 A1 = Lerp(p0, p1, t, t0, t1);
            glm::vec2 A2 = Lerp(p1, p2, t, t1, t2);
            glm::vec2 A3 = Lerp(p2, p3, t, t2, t3);
            glm::vec2 B1 = Lerp(A1, A2, t, t0, t2);
            glm::vec2 B2 = Lerp(A2, A3, t, t1, t3);
            glm::vec2 C = Lerp(B1, B2, t, t1, t2); // Final position

            // --- ANALYTICAL TANGENT (New) ---
            // Velocities of base points are 0, as they are constants
            glm::vec2 zeroV(0.0f);

            // Level 1 derivatives (velocities between control points)
            glm::vec2 VA1 = LerpDerivative(p0, p1, zeroV, zeroV, t, t0, t1);
            glm::vec2 VA2 = LerpDerivative(p1, p2, zeroV, zeroV, t, t1, t2);
            glm::vec2 VA3 = LerpDerivative(p2, p3, zeroV, zeroV, t, t2, t3);

            // Level 2 derivatives
            glm::vec2 VB1 = LerpDerivative(A1, A2, VA1, VA2, t, t0, t2);
            glm::vec2 VB2 = LerpDerivative(A2, A3, VA2, VA3, t, t1, t3);

            // Level 3 derivative (Final tangent/velocity)
            glm::vec2 Tangent = LerpDerivative(B1, B2, VB1, VB2, t, t1, t2);

            SplinePoint sp;
            sp.position = C;
            // Important: normalize the tangent! If length is 0, take the previous or default one.
            if (glm::length(Tangent) > 1e-6f) {
                sp.tangent = glm::normalize(Tangent);
            }
            else if (!resultPath.empty()) {
                sp.tangent = resultPath.back().tangent;
            }
            else {
                sp.tangent = glm::vec2(1, 0); // In case of the first point with zero velocity
            }

            resultPath.push_back(sp);
        }
    }

    // Add the very last point.
    // Its tangent will be the same as the previous calculated point.
    if (!resultPath.empty()) {
        SplinePoint lastSP;
        lastSP.position = originalPoints.back();
        lastSP.tangent = resultPath.back().tangent;
        resultPath.push_back(lastSP);
    }

    return resultPath;
}

std::vector<glm::vec2> GenerateTriangleStripFromLine(const std::vector<SplinePoint>& splinePoints, float width)
{
    std::vector<glm::vec2> triangleStripPoints;
    if (splinePoints.size() < 2) return triangleStripPoints;

    float halfWidth = width * 0.5f;

    for (size_t i = 0; i < splinePoints.size(); i++)
    {
        glm::vec2 tangent = splinePoints[i].tangent;
        glm::vec2 normal(-tangent.y, tangent.x); // Standard perpendicular

        // --- MITER LIMIT LOGIC ---
        // If this is a point inside the path, calculate the average normal between the current and previous segment
        if (i > 0 && i < splinePoints.size() - 1) {
            glm::vec2 prevNormal(-splinePoints[i - 1].tangent.y, splinePoints[i - 1].tangent.x);
            glm::vec2 miter = glm::normalize(prevNormal + normal);

            // Correction length: 1 / cos(angle/2)
            float dot = glm::dot(miter, normal);
            float length = halfWidth / std::max(0.1f, dot); // Protection against division by 0

            // Limit the length (to avoid infinite spikes)
            if (length > halfWidth * 2.0f) length = halfWidth * 2.0f;

            normal = miter * (length / halfWidth); // Update normal considering the length
        }

        glm::vec2 leftPoint = splinePoints[i].position + normal * halfWidth;
        glm::vec2 rightPoint = splinePoints[i].position - normal * halfWidth;

        triangleStripPoints.push_back(leftPoint);
        triangleStripPoints.push_back(rightPoint);
    }
    return triangleStripPoints;
}


std::vector<glm::vec2> SmoothPath(const std::vector<glm::vec2>& rawPoints, int windowSize) {
    if (rawPoints.size() < windowSize) return rawPoints;
    std::vector<glm::vec2> smoothed;

    for (size_t i = 0; i < rawPoints.size(); i++) {
        glm::vec2 avg(0.0f);
        int count = 0;
        for (int j = -windowSize / 2; j <= windowSize / 2; j++) {
            int idx = (int)i + j;
            if (idx >= 0 && idx < (int)rawPoints.size()) {
                avg += rawPoints[idx];
                count++;
            }
        }
        smoothed.push_back(avg / (float)count);
    }
    return smoothed;
}

// --- SIMPLIFICATION ALGORITHMS ---

std::vector<glm::vec2> FilterPointsByDistance(const std::vector<glm::vec2>& points, float minDistance) {
    if (points.empty()) return points;
    std::vector<glm::vec2> result;
    result.push_back(points[0]);
    for (size_t i = 1; i < points.size(); i++) {
        if (glm::distance(points[i], result.back()) >= minDistance) {
            result.push_back(points[i]);
        }
    }
    return result;
}

// Helper for Douglas-Peucker
static float PerpendicularDistance(const glm::vec2& p, const glm::vec2& lineStart, const glm::vec2& lineEnd) {
    float dx = lineEnd.x - lineStart.x;
    float dy = lineEnd.y - lineStart.y;
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 1e-6f) {
        return glm::distance(p, lineStart);
    }
    return std::abs(dy * p.x - dx * p.y + lineEnd.x * lineStart.y - lineEnd.y * lineStart.x) / mag;
}

static void DouglasPeuckerRecursive(const std::vector<glm::vec2>& points, int first, int last, float tolerance, std::vector<bool>& keep) {
    float maxDist = 0.0f;
    int index = 0;

    for (int i = first + 1; i < last; i++) {
        float dist = PerpendicularDistance(points[i], points[first], points[last]);
        if (dist > maxDist) {
            maxDist = dist;
            index = i;
        }
    }

    if (maxDist > tolerance) {
        keep[index] = true;
        DouglasPeuckerRecursive(points, first, index, tolerance, keep);
        DouglasPeuckerRecursive(points, index, last, tolerance, keep);
    }
}

std::vector<glm::vec2> SimplifyPath(const std::vector<glm::vec2>& points, float tolerance) {
    if (points.size() < 3) return points;

    std::vector<bool> keep(points.size(), false);
    keep[0] = true;
    keep[points.size() - 1] = true;

    DouglasPeuckerRecursive(points, 0, (int)points.size() - 1, tolerance, keep);

    std::vector<glm::vec2> result;
    for (size_t i = 0; i < points.size(); i++) {
        if (keep[i]) {
            result.push_back(points[i]);
        }
    }
    return result;
}

// New algorithm 
std::vector<SplinePoint> InterpolateRoundedPolyline(const std::vector<glm::vec2>& points, float radius, int segmentsPerCorner)
{

    std::vector<SplinePoint> result;
    if (points.size() < 2) return result;

    size_t n = points.size();
    // Check for closed loop
    bool isClosed = (glm::distance(points.front(), points.back()) < 1e-4f);
    size_t count = isClosed ? n - 1 : n;

    for (size_t i = 0; i < count; i++) {
        glm::vec2 p1 = points[i];
        glm::vec2 p0 = points[(i + count - 1) % count]; // Previous
        glm::vec2 p2 = points[(i + 1) % count];         // Next

        if (radius <= 0.0f) {
            // Corner Radius = 0 mode (as in the second photo)
            SplinePoint sp;
            sp.position = p1;
            sp.tangent = glm::normalize(p2 - p1);
            result.push_back(sp);
            continue;
        }

        // Direction vectors
        glm::vec2 v1 = glm::normalize(p0 - p1);
        glm::vec2 v2 = glm::normalize(p2 - p1);

        // Distance to rounding points (must not exceed half the segment length)
        float d1 = glm::distance(p0, p1);
        float d2 = glm::distance(p1, p2);
        float actualRadius = std::min({ radius, d1 * 0.5f, d2 * 0.5f });

        // Start and end points of rounding
        glm::vec2 startPoint = p1 + v1 * actualRadius;
        glm::vec2 endPoint = p1 + v2 * actualRadius;

        // Build Quadratic Bezier curve for the corner
        for (int j = 0; j <= segmentsPerCorner; j++) {
            float t = (float)j / (float)segmentsPerCorner;

            // Bezier formula: (1-t)^2*P0 + 2(1-t)t*P1 + t^2*P2
            glm::vec2 pos = std::pow(1.0f - t, 2.0f) * startPoint +
                2.0f * (1.0f - t) * t * p1 +
                std::pow(t, 2.0f) * endPoint;

            // Derivative (tangent) for correct track thickness
            glm::vec2 tangent = 2.0f * (1.0f - t) * (p1 - startPoint) +
                2.0f * t * (endPoint - p1);

            SplinePoint sp;
            sp.position = pos;
            sp.tangent = glm::normalize(tangent);
            result.push_back(sp);
        }
    }

    if (isClosed) result.push_back(result.front());
    return result;
}