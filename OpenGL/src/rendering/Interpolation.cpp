#include "Interpolation.h"
#include <cmath>
#include <iostream>
#include <algorithm>

// Structure to store the interpolation result


// Helper: Calculates the time knot (unchanged)
float getTimeKnot(float t, const glm::vec2& p0, const glm::vec2& p1, float alpha)
{
    float a = std::pow((p1.x - p0.x), 2.0f) + std::pow((p1.y - p0.y), 2.0f);
    float b = std::pow(a, 0.5f);
    float c = std::pow(b, alpha);
    if (c < 1e-4f) c = 1.0f;
    return (c + t);
}

// Helper: Standard Linear Interpolation (unchanged)
glm::vec2 lerp(const glm::vec2& p0, const glm::vec2& p1, float t, float t0, float t1)
{
    if (std::abs(t1 - t0) < 1e-5f) return p0;
    return (p0 * (t1 - t) + p1 * (t - t0)) / (t1 - t0);
}

// NEW FUNCTION: Calculates derivative (velocity) for lerp
// v0, v1 - velocities (derivatives) at points p0 and p1
glm::vec2 lerpDerivative(const glm::vec2& p0, const glm::vec2& p1,
    const glm::vec2& v0, const glm::vec2& v1,
    float t, float t0, float t1)
{
    float dt = t1 - t0;
    if (std::abs(dt) < 1e-5f) return glm::vec2(0.0f);
    // Derivative formula for recursive lerp
    return ((p1 - p0) + v0 * (t1 - t) + v1 * (t - t0)) / dt;
}




// Change return type to std::vector<SplinePoint>
std::vector<SplinePoint> interpolatePointsWithTangents(const std::vector<glm::vec2>& original_points, int points_per_segment, float alpha)
{
    std::vector<SplinePoint> result_path;
    if (original_points.size() < 2) return result_path;

    for (size_t i = 0; i < original_points.size() - 1; i++)
    {
        glm::vec2 p0, p1, p2, p3;
        p1 = original_points[i];
        p2 = original_points[i + 1];

        if (i == 0) p0 = p1 - (p2 - p1); else p0 = original_points[i - 1];
        if (i + 2 < original_points.size()) p3 = original_points[i + 2]; else p3 = p2 + (p2 - p1);

        float t0 = 0.0f;
        float t1 = getTimeKnot(t0, p0, p1, alpha);
        float t2 = getTimeKnot(t1, p1, p2, alpha);
        float t3 = getTimeKnot(t2, p2, p3, alpha);

        for (int j = 0; j < points_per_segment; j++)
        {
            float t = t1 + ((t2 - t1) * ((float)j / (float)points_per_segment));

            // --- Position (as before) ---
            glm::vec2 A1 = lerp(p0, p1, t, t0, t1);
            glm::vec2 A2 = lerp(p1, p2, t, t1, t2);
            glm::vec2 A3 = lerp(p2, p3, t, t2, t3);
            glm::vec2 B1 = lerp(A1, A2, t, t0, t2);
            glm::vec2 B2 = lerp(A2, A3, t, t1, t3);
            glm::vec2 C = lerp(B1, B2, t, t1, t2); // Final position

            // --- ANALYTICAL tangent (New) ---
            // Velocities of base points are 0, as they are constants
            glm::vec2 zero_velocity(0.0f);

            // Level 1 derivatives (velocities between control points)
            glm::vec2 VA1 = lerpDerivative(p0, p1, zero_velocity, zero_velocity, t, t0, t1);
            glm::vec2 VA2 = lerpDerivative(p1, p2, zero_velocity, zero_velocity, t, t1, t2);
            glm::vec2 VA3 = lerpDerivative(p2, p3, zero_velocity, zero_velocity, t, t2, t3);

            // Level 2 derivatives
            glm::vec2 VB1 = lerpDerivative(A1, A2, VA1, VA2, t, t0, t2);
            glm::vec2 VB2 = lerpDerivative(A2, A3, VA2, VA3, t, t1, t3);

            // Level 3 derivative (Final tangent/velocity)
            glm::vec2 tangent = lerpDerivative(B1, B2, VB1, VB2, t, t1, t2);

            SplinePoint sp;
            sp.position = C;
            // Important: normalize the tangent! If length is 0, take the previous or default one.
            if (glm::length(tangent) > 1e-6f) {
                sp.tangent = glm::normalize(tangent);
            }
            else if (!result_path.empty()) {
                sp.tangent = result_path.back().tangent;
            }
            else {
                sp.tangent = glm::vec2(1, 0); // In case of the first point with zero velocity
            }

            result_path.push_back(sp);
        }
    }

    // Add the very last point.
    // Its tangent will be the same as the previous calculated point.
    if (!result_path.empty()) {
        SplinePoint last_point;
        last_point.position = original_points.back();
        last_point.tangent = result_path.back().tangent;
        result_path.push_back(last_point);
    }

    return result_path;
}

std::vector<glm::vec2> generateTriangleStripFromLine(const std::vector<SplinePoint>& spline_points, float width)
{
    std::vector<glm::vec2> triangle_strip_points;
    if (spline_points.size() < 2) return triangle_strip_points;

    float half_width = width * 0.5f;

    for (size_t i = 0; i < spline_points.size(); i++)
    {
        glm::vec2 tangent = spline_points[i].tangent;
        glm::vec2 normal(-tangent.y, tangent.x); // Standard perpendicular

        // --- MITER LIMIT LOGIC ---
        // If this is a point inside the path, calculate the average normal between the current and previous segment
        if (i > 0 && i < spline_points.size() - 1) {
            glm::vec2 prev_normal(-spline_points[i - 1].tangent.y, spline_points[i - 1].tangent.x);
            glm::vec2 miter = glm::normalize(prev_normal + normal);

            // Correction length: 1 / cos(angle/2)
            float dot = glm::dot(miter, normal);
            float length = half_width / std::max(0.1f, dot); // Protection against division by 0

            // Limit the length (to avoid infinite spikes)
            if (length > half_width * 2.0f) length = half_width * 2.0f;

            normal = miter * (length / half_width); // Update normal considering the length
        }

        glm::vec2 left_point = spline_points[i].position + normal * half_width;
        glm::vec2 right_point = spline_points[i].position - normal * half_width;

        triangle_strip_points.push_back(left_point);
        triangle_strip_points.push_back(right_point);
    }
    return triangle_strip_points;
}


std::vector<glm::vec2> smoothPath(const std::vector<glm::vec2>& raw_points, int window_size) {
    if (raw_points.size() < window_size) return raw_points;
    std::vector<glm::vec2> smoothed_points;

    for (size_t i = 0; i < raw_points.size(); i++) {
        glm::vec2 average(0.0f);
        int count = 0;
        for (int j = -window_size / 2; j <= window_size / 2; j++) {
            int idx = (int)i + j;
            if (idx >= 0 && idx < (int)raw_points.size()) {
                average += raw_points[idx];
                count++;
            }
        }
        smoothed_points.push_back(average / (float)count);
    }
    return smoothed_points;
}

// --- SIMPLIFICATION ALGORITHMS ---

std::vector<glm::vec2> filterPointsByDistance(const std::vector<glm::vec2>& points, float min_distance) {
    if (points.empty()) return points;
    std::vector<glm::vec2> result;
    result.push_back(points[0]);
    for (size_t i = 1; i < points.size(); i++) {
        if (glm::distance(points[i], result.back()) >= min_distance) {
            result.push_back(points[i]);
        }
    }
    return result;
}

// Helper for Douglas-Peucker
static float perpendicularDistance(const glm::vec2& p, const glm::vec2& line_start, const glm::vec2& line_end) {
    float dx = line_end.x - line_start.x;
    float dy = line_end.y - line_start.y;
    float magnitude = std::sqrt(dx * dx + dy * dy);
    if (magnitude < 1e-6f) {
        return glm::distance(p, line_start);
    }
    return std::abs(dy * p.x - dx * p.y + line_end.x * line_start.y - line_end.y * line_start.x) / magnitude;
}

static void douglasPeuckerRecursive(const std::vector<glm::vec2>& points, int first, int last, float tolerance, std::vector<bool>& keep) {
    float max_distance = 0.0f;
    int point_index = 0;

    for (int i = first + 1; i < last; i++) {
        float dist = perpendicularDistance(points[i], points[first], points[last]);
        if (dist > max_distance) {
            max_distance = dist;
            point_index = i;
        }
    }

    if (max_distance > tolerance) {
        keep[point_index] = true;
        douglasPeuckerRecursive(points, first, point_index, tolerance, keep);
        douglasPeuckerRecursive(points, point_index, last, tolerance, keep);
    }
}

std::vector<glm::vec2> simplifyPath(const std::vector<glm::vec2>& points, float tolerance) {
    if (points.size() < 3) return points;

    std::vector<bool> keep(points.size(), false);
    keep[0] = true;
    keep[points.size() - 1] = true;

    douglasPeuckerRecursive(points, 0, (int)points.size() - 1, tolerance, keep);

    std::vector<glm::vec2> result;
    for (size_t i = 0; i < points.size(); i++) {
        if (keep[i]) {
            result.push_back(points[i]);
        }
    }
    return result;
}

// New algorithm 
std::vector<SplinePoint> interpolateRoundedPolyline(const std::vector<glm::vec2>& points, float radius, int segments_per_corner)
{

    std::vector<SplinePoint> result;
    if (points.size() < 2) return result;

    size_t n = points.size();
    // Check for closed loop
    bool is_closed = (glm::distance(points.front(), points.back()) < 1e-4f);
    size_t count = is_closed ? n - 1 : n;

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
        float actual_radius = std::min({ radius, d1 * 0.5f, d2 * 0.5f });

        // Start and end points of rounding
        glm::vec2 start_point = p1 + v1 * actual_radius;
        glm::vec2 end_point = p1 + v2 * actual_radius;

        // Build Quadratic Bezier curve for the corner
        for (int j = 0; j <= segments_per_corner; j++) {
            float t = (float)j / (float)segments_per_corner;

            // Bezier formula: (1-t)^2*P0 + 2(1-t)t*P1 + t^2*P2
            glm::vec2 pos = std::pow(1.0f - t, 2.0f) * start_point +
                2.0f * (1.0f - t) * t * p1 +
                std::pow(t, 2.0f) * end_point;

            // Derivative (tangent) for correct track thickness
            glm::vec2 tangent = 2.0f * (1.0f - t) * (p1 - start_point) +
                2.0f * t * (end_point - p1);

            SplinePoint sp;
            sp.position = pos;
            sp.tangent = glm::normalize(tangent);
            result.push_back(sp);
        }
    }

    if (is_closed) result.push_back(result.front());
    return result;
}

// ============================================================================
// TRACK CENTERING FUNCTIONS
// ============================================================================

TrackCenterInfo calculateTrackCenter(const std::vector<glm::vec2>& points)
{
    TrackCenterInfo info;
    info.geometric_center = glm::vec2(0.0f, 0.0f);
    info.offset = glm::vec2(0.0f, 0.0f);
    info.is_closed = false;
    
    if (points.size() < 2) {
        return info;
    }
    
    // Check if track is closed
    info.is_closed = (glm::distance(points.front(), points.back()) < 1e-4f);
    
    // Calculate geometric center (average of all points)
    glm::vec2 sum(0.0f, 0.0f);
    size_t count = info.is_closed ? points.size() - 1 : points.size(); // Exclude duplicate if closed
    
    for (size_t i = 0; i < count; i++) {
        sum += points[i];
    }
    
    info.geometric_center = sum / (float)count;
    
    // Offset needed to move center to (0, 0)
    info.offset = -info.geometric_center;
    
    std::cout << "[TRACK CENTER] Calculated center: (" << info.geometric_center.x << ", " << info.geometric_center.y << ")" << std::endl;
    std::cout << "[TRACK CENTER] Track is " << (info.is_closed ? "CLOSED" : "OPEN") << std::endl;
    std::cout << "[TRACK CENTER] Offset to apply: (" << info.offset.x << ", " << info.offset.y << ")" << std::endl;
    
    return info;
}

void recenterTrack(std::vector<glm::vec2>& points, const TrackCenterInfo& center_info)
{
    if (points.empty()) return;
    
    std::cout << "[TRACK CENTER] Recentering " << points.size() << " points..." << std::endl;
    
    // Apply offset to all points
    for (auto& point : points) {
        point += center_info.offset;
    }
    
    std::cout << "[TRACK CENTER] Track recentered successfully!" << std::endl;
}
