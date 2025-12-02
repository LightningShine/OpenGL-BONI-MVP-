#pragma once
#include "Input.h"
#include "EDITH.h"
#include <cmath>

// Helper: Calculates the time knot for a point based on distance and alpha.
// t: The time of the previous point.
// p0, p1: The two points to measure distance between.
// alpha: The exponent parameter.
float GetT(float t, const glm::vec2& p0, const glm::vec2& p1, float alpha)
{
    float a = std::pow((p1.x - p0.x), 2.0f) + std::pow((p1.y - p0.y), 2.0f);
    float b = std::pow(a, 0.5f);
    float c = std::pow(b, alpha);

    // Prevent division by zero if points are identical
    if (c < 1e-4f) c = 1.0f;

    return (c + t);
}

// Helper: Standard Linear Interpolation
glm::vec2 Lerp(const glm::vec2& p0, const glm::vec2& p1, float t, float t0, float t1)
{
    // Avoid division by zero
    if (std::abs(t1 - t0) < 1e-5f) return p0;

    return (p0 * (t1 - t) + p1 * (t - t0)) / (t1 - t0);
}

std::vector<glm::vec2> InterpolatePoints(const std::vector<glm::vec2>& originalPoints, int pointsPerSegment, float alpha)
{
    if (originalPoints.size() < 2)
    {
        return originalPoints;
    }

    std::vector<glm::vec2> smoothedPoints;

    for (size_t i = 0; i < originalPoints.size() - 1; i++)
    {
        glm::vec2 p0, p1, p2, p3;

        p1 = originalPoints[i];
        p2 = originalPoints[i + 1];

        // Handle start point (duplicate if first)
        if (i == 0)
            p0 = p1 - (p2 - p1); // Extend backwards linearly instead of simple duplication for better start curvature
        else
            p0 = originalPoints[i - 1];

        // Handle end point (duplicate if last)
        if (i + 2 < originalPoints.size())
            p3 = originalPoints[i + 2];
        else
            p3 = p2 + (p2 - p1); // Extend forwards linearly

        // 1. Calculate time knots (t0, t1, t2, t3)
        // This is where alpha plays its role.
        float t0 = 0.0f;
        float t1 = GetT(t0, p0, p1, alpha);
        float t2 = GetT(t1, p1, p2, alpha);
        float t3 = GetT(t2, p2, p3, alpha);

        // 2. Generate points between p1 and p2
        for (int j = 0; j < pointsPerSegment; j++)
        {
            // t runs from t1 to t2
            float t = t1 + ((t2 - t1) * ((float)j / (float)pointsPerSegment));

            // Barry-Goldman pyramidal formulation
            // Level 1
            glm::vec2 A1 = Lerp(p0, p1, t, t0, t1);
            glm::vec2 A2 = Lerp(p1, p2, t, t1, t2);
            glm::vec2 A3 = Lerp(p2, p3, t, t2, t3);

            // Level 2
            glm::vec2 B1 = Lerp(A1, A2, t, t0, t2);
            glm::vec2 B2 = Lerp(A2, A3, t, t1, t3);

            // Level 3 (Result)
            glm::vec2 C = Lerp(B1, B2, t, t1, t2);

            smoothedPoints.push_back(C);
        }
    }

    // Add the very last point
    smoothedPoints.push_back(originalPoints.back());

    return smoothedPoints;
}

std::vector<glm::vec2> GenerateTriangleStripFromLine(const std::vector<glm::vec2>& linePoints, float width)
{
    std::vector<glm::vec2> triangleStripPoints;
    
    if (linePoints.size() < 2)
    {
        return triangleStripPoints;
    }

    for (size_t i = 0; i < linePoints.size(); i++)
    {
        glm::vec2 direction;
        
        if (i == 0)
        {
            // First point: use direction to next point
            direction = glm::normalize(linePoints[1] - linePoints[0]);
        }
        else if (i == linePoints.size() - 1)
        {
            // Last point: use direction from previous point
            direction = glm::normalize(linePoints[i] - linePoints[i - 1]);
        }
        else
        {
            // Middle points: average direction
            glm::vec2 dir1 = glm::normalize(linePoints[i] - linePoints[i - 1]);
            glm::vec2 dir2 = glm::normalize(linePoints[i + 1] - linePoints[i]);
            direction = glm::normalize(dir1 + dir2);
        }
        
        // Calculate perpendicular vector (rotate 90 degrees)
        glm::vec2 perpendicular(-direction.y, direction.x);
        
        // Generate left and right edge points
        glm::vec2 leftPoint = linePoints[i] - perpendicular * width;
        glm::vec2 rightPoint = linePoints[i] + perpendicular * width;
        
        // Add points in triangle strip order
        triangleStripPoints.push_back(leftPoint);
        triangleStripPoints.push_back(rightPoint);
    }
    
    return triangleStripPoints;
}