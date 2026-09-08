#pragma once
#include "input/Input.h"
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

struct SplinePoint {
    glm::vec2 position;
    glm::vec2 tangent; // Нормализованный вектор направления
};

glm::vec2 lerpDerivative(const glm::vec2& p0, const glm::vec2& p1,
    const glm::vec2& v0, const glm::vec2& v1,
    float t, float t0, float t1);

std::vector<SplinePoint> interpolatePointsWithTangents(const std::vector<glm::vec2>& original_points, int points_per_segment, float alpha =0.5f);

std::vector<SplinePoint> interpolateRoundedPolyline(const std::vector<glm::vec2>& points, float radius, int segments_per_corner);

std::vector<glm::vec2> smoothPath(const std::vector<glm::vec2>& raw_points, int window_size = 3);

std::vector<glm::vec2> simplifyPath(const std::vector<glm::vec2>& points, float tolerance);

std::vector<glm::vec2> filterPointsByDistance(const std::vector<glm::vec2>& points, float min_distance);

std::vector<glm::vec2> generateTriangleStripFromLine(const std::vector<SplinePoint>& spline_points, float width);

// Track centering
struct TrackCenterInfo {
    glm::vec2 geometric_center;  // Геометрический центр трека
    glm::vec2 offset;            // Смещение которое нужно применить к точкам
    bool is_closed;              // Замкнут ли трек
};

TrackCenterInfo calculateTrackCenter(const std::vector<glm::vec2>& points);
void recenterTrack(std::vector<glm::vec2>& points, const TrackCenterInfo& center_info);

// ----------------------------------------------------------------------------
// Публикация геометрии трека.
//
// g_smooth_track_points читают горячие пути (проекция позиции машины на трек —
// по вызову на каждый пакет телеметрии), поэтому потребители держат собственные
// снимки и обновляют их только при смене версии. Любая запись геометрии обязана
// идти через publish_smooth_track_points(): она и берёт g_track_mutex, и двигает
// версию. Прямое присваивание глобалу оставит чужие снимки со старыми точками.
// ----------------------------------------------------------------------------
void publish_smooth_track_points(std::vector<SplinePoint> points);

// Текущая версия геометрии. Меняется — значит трек перезаписан.
uint32_t track_geometry_generation();

// Track render offset (normalized units).
// If track points are shifted (e.g. via recenterTrack), vehicles must be rendered with the same offset
// to stay aligned with the track.
extern glm::vec2 g_track_render_offset;
glm::vec2 getTrackRenderOffset();

// Dual-edge track utilities
std::vector<glm::vec2> resamplePolyline(const std::vector<glm::vec2>& pts, int n);
void alignPolylineDirection(std::vector<glm::vec2>& b, const std::vector<glm::vec2>& reference);
std::vector<glm::vec2> generateTriangleStripFromEdges(
    const std::vector<glm::vec2>& left,
    const std::vector<glm::vec2>& right);
