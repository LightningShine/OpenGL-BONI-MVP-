#pragma once

#include <vector>
#include <mutex>
#include <glm/glm.hpp>
#include <glad/glad.h>

// Forward declarations
struct SplinePoint;

namespace TrackRenderer
{
    void rebuildTrackCache(const std::vector<glm::vec2>& points, std::mutex& points_mutex);

    // Build GPU cache directly from already-smoothed spline points (e.g. received from server).
    // Must be called from the main thread (OpenGL context thread).
    void rebuildTrackCacheFromSplinePoints(const std::vector<SplinePoint>& smoothPoints);
    
    void renderCachedTrack(GLuint shader_program);
    
    bool isTrackCacheValid();
    
    void clearTrackCache();

    size_t getBorderVertexCount();
    
    size_t getAsphaltVertexCount();
    
    // Start/Finish Line rendering
    void renderStartFinishLine(GLuint shader_program, const glm::mat4& viewProjection);
    
    // Gray line rendering (called AFTER checkered flag to render on top)
    void renderStartFinishGrayLine(GLuint shader_program);
    
    void clearStartFinishLine();

    // Lightweight preview for single-edge recording.
    void rebuildTrackPreviewCache(const std::vector<glm::vec2>& points, std::mutex& points_mutex);

    // Build final GPU cache from two recorded edge polylines.
    void rebuildTrackCacheFromEdges(
        const std::vector<glm::vec2>& left,
        const std::vector<glm::vec2>& right);

    // Live preview during right-edge recording: shows the forming track mesh.
    void rebuildDualEdgePreviewCache(
        const std::vector<glm::vec2>& left,
        const std::vector<glm::vec2>& right);
}
