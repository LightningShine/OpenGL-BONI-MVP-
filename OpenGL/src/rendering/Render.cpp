#include "Render.h"
#include "Interpolation.h"
#include "../Config.h"
#include "../vehicle/Vehicle.h"
#include "../input/Input.h"  // ? ??? g_is_map_loaded
#include <iostream>

// ============================================================================
// EXTERNAL GLOBALS (????????? ? ?????? ??????)
// ============================================================================
extern std::atomic<bool> g_is_map_loaded;       // ?? Input.cpp
extern std::vector<SplinePoint> g_smooth_track_points;  // ?? main.cpp

namespace TrackRenderer
{
    // ============================================================================
    // INTERNAL CACHE STORAGE
    // ============================================================================
    
    // ??? ?????????
    static std::vector<glm::vec2> s_cached_border_layer;
    static std::vector<glm::vec2> s_cached_asphalt_layer;
    static bool s_track_cache_valid = false;
    
    // OpenGL ??????? (????????? ???? ???!)
    static GLuint s_track_vao = 0;
    static GLuint s_vbo_border = 0;
    static GLuint s_vbo_asphalt = 0;
    
    // ???????????? uniform locations
    static GLint s_cached_colorLoc = -1;
    static GLint s_cached_alphaLoc = -1;
    static GLuint s_cached_shader = 0;
    
    // ============================================================================
    // PUBLIC FUNCTIONS
    // ============================================================================
    
    void rebuildTrackCache(const std::vector<glm::vec2>& points, std::mutex& points_mutex)
    {
        std::lock_guard<std::mutex> lock(points_mutex);
        
        // ????????: ???? ????? ???? - ??????? ???
        if (points.size() <= 1)
        {
            s_cached_border_layer.clear();
            s_cached_asphalt_layer.clear();
            s_track_cache_valid = false;
            g_is_map_loaded = false;
            
            std::cout << "[CACHE] Track cache cleared (no points)" << std::endl;
            return;
        }
        
        std::cout << "[CACHE] Rebuilding track cache with " << points.size() << " points..." << std::endl;
        
        // ========================================================================
        // STEP 1: Filter noise (??????? ????? ??????? ??????? ??????)
        // ========================================================================
        std::vector<glm::vec2> filteredPoints = filterPointsByDistance(points, 0.05f);
        std::cout << "[CACHE]   Step 1: Filtered to " << filteredPoints.size() << " points" << std::endl;
        
        // ========================================================================
        // STEP 2: Simplify path (??????? ??????????? ?? ?????? ????????)
        // ========================================================================
        std::vector<glm::vec2> simplifiedPoints = simplifyPath(filteredPoints, 0.02f);
        std::cout << "[CACHE]   Step 2: Simplified to " << simplifiedPoints.size() << " points" << std::endl;
        
        // ========================================================================
        // STEP 3: Generate rounded corners (????????? ????)
        // ========================================================================
        std::vector<SplinePoint> smoothPoints = interpolateRoundedPolyline(
            simplifiedPoints, 
            TrackConstants::TRACK_CORNER_RADIUS, 
            TrackConstants::TRACK_CORNER_SEGMENTS
        );
        std::cout << "[CACHE]   Step 3: Interpolated to " << smoothPoints.size() << " smooth points" << std::endl;
        
        // ????????? ??? ????????? ?????
        g_smooth_track_points = smoothPoints;
        
        std::cout << "[CACHE]   g_smooth_track_points filled with " << g_smooth_track_points.size() << " points" << std::endl;
        
        // ========================================================================
        // STEP 4: Generate triangle strips
        // ========================================================================
        s_cached_border_layer = generateTriangleStripFromLine(smoothPoints, TrackConstants::TRACK_BORDER_WIDTH);
        s_cached_asphalt_layer = generateTriangleStripFromLine(smoothPoints, TrackConstants::TRACK_ASPHALT_WIDTH);
        
        std::cout << "[CACHE]   Step 4: Generated geometry (border: " << s_cached_border_layer.size() 
                  << ", asphalt: " << s_cached_asphalt_layer.size() << " vertices)" << std::endl;
        
        // ========================================================================
        // STEP 5: Upload to GPU (NO vertex attribute setup!)
        // ========================================================================
        
        // Create VAO/VBO if they don't exist
        if (s_track_vao == 0)
        {
            glGenVertexArrays(1, &s_track_vao);
            glGenBuffers(1, &s_vbo_border);
            glGenBuffers(1, &s_vbo_asphalt);
            std::cout << "[CACHE]   Created OpenGL objects (VAO: " << s_track_vao 
                      << ", VBO border: " << s_vbo_border 
                      << ", VBO asphalt: " << s_vbo_asphalt << ")" << std::endl;
        }
        
        // Upload border data to GPU
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_border);
        glBufferData(GL_ARRAY_BUFFER, 
                     s_cached_border_layer.size() * sizeof(glm::vec2),
                     s_cached_border_layer.data(),
                     GL_STATIC_DRAW);
        
        // Upload asphalt data to GPU
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_asphalt);
        glBufferData(GL_ARRAY_BUFFER,
                     s_cached_asphalt_layer.size() * sizeof(glm::vec2),
                     s_cached_asphalt_layer.data(),
                     GL_STATIC_DRAW);
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        s_track_cache_valid = true;
        g_is_map_loaded = true;
        
        std::cout << "[CACHE] ? Track uploaded to GPU (STATIC buffers)" << std::endl;
        std::cout << "[CACHE]   Memory: ~" 
                  << ((s_cached_border_layer.size() + s_cached_asphalt_layer.size()) * sizeof(glm::vec2) / 1024)
                  << " KB" << std::endl;
    }
    
    void renderCachedTrack(GLuint shader_program)
    {
        if (!s_track_cache_valid || s_track_vao == 0)
            return;
        
        // Cache uniform locations (once per shader)
        if (s_cached_shader != shader_program)
        {
            s_cached_colorLoc = glGetUniformLocation(shader_program, "uColor");
            s_cached_alphaLoc = glGetUniformLocation(shader_program, "uAlpha");
            s_cached_shader = shader_program;
        }
        
        static bool printed = false;
        if (!printed) {
            std::cout << "[CACHE] ? Rendering from GPU (ZERO CPU->GPU transfer!)" << std::endl;
            printed = true;
        }
        
        // Bind track VAO
        glBindVertexArray(s_track_vao);
        
        // ========================================================================
        // Draw border layer
        // ========================================================================
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_border);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
        glEnableVertexAttribArray(0);
        
        glUniform3f(s_cached_colorLoc, 1.0f, 1.0f, 1.0f);
        glUniform1f(s_cached_alphaLoc, 1.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(s_cached_border_layer.size()));
        
        // ========================================================================
        // Draw asphalt layer
        // ========================================================================
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_asphalt);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
        glEnableVertexAttribArray(0);
        
        glUniform3f(s_cached_colorLoc, 0.3f, 0.3f, 0.3f);
        glUniform1f(s_cached_alphaLoc, 1.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(s_cached_asphalt_layer.size()));
        
        // Unbind to avoid state leakage
        glBindVertexArray(0);
    }
    
    bool isTrackCacheValid()
    {
        return s_track_cache_valid;
    }
    
    void clearTrackCache()
    {
        s_cached_border_layer.clear();
        s_cached_asphalt_layer.clear();
        s_track_cache_valid = false;
        g_is_map_loaded = false;
        
        // ??????? OpenGL ???????
        if (s_track_vao != 0)
        {
            glDeleteVertexArrays(1, &s_track_vao);
            glDeleteBuffers(1, &s_vbo_border);
            glDeleteBuffers(1, &s_vbo_asphalt);
            s_track_vao = 0;
            s_vbo_border = 0;
            s_vbo_asphalt = 0;
            std::cout << "[CACHE] OpenGL objects deleted" << std::endl;
        }
        
        std::cout << "[CACHE] Track cache cleared" << std::endl;
    }
    
    size_t getBorderVertexCount()
    {
        return s_cached_border_layer.size();
    }
    
    size_t getAsphaltVertexCount()
    {
        return s_cached_asphalt_layer.size();
    }
}
