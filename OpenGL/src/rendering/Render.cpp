#include "Render.h"
#include "Interpolation.h"
#include "../Config.h"
#include "../vehicle/Vehicle.h"
#include "../input/Input.h"  //  g_is_map_loaded
#include <iostream>

// ============================================================================
// EXTERNAL GLOBALS 
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
    static std::vector<glm::vec2> s_debug_line;  // Debug gray line
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
    // START/FINISH LINE STORAGE (Dedicated Shader, Racing Checkered Pattern)
    // ============================================================================
    
    static GLuint s_start_line_vao = 0;
    static GLuint s_vbo_start_quad = 0;
    static GLuint s_start_line_shader = 0;  // Dedicated shader program
    static bool s_start_line_initialized = false;
    
    // Dedicated shaders for start/finish line (compiled once)
    static const char* s_start_line_vertex_shader = R"(
        #version 460 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoord;
        
        uniform mat4 projection;
        
        out vec2 vUV;
        
        void main()
        {
            gl_Position = projection * vec4(aPos, 0.0, 1.0);
            vUV = aTexCoord;
        }
    )";
    
    static const char* s_start_line_fragment_shader = R"(
        #version 460 core
        out vec4 FragColor;
        in vec2 vUV;
        
        void main()
        {
            // Racing Checkered Flag Pattern: 5 rows × 5 columns (perfect square)
            // As shown in reference image - square checkered pattern
            vec2 uv = vUV * vec2(5.0, 5.0);
            
            // Standard checker pattern math
            float check = mod(floor(uv.x) + floor(uv.y), 2.0);
            
            // Pure Black/White contrast (racing flag)
            FragColor = vec4(vec3(check), 1.0);  // Full opacity
        }
    )";
    
    // Compile shader helper function (forward declaration before use)
    static GLuint compileStartLineShader()
    {
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &s_start_line_vertex_shader, NULL);
        glCompileShader(vertexShader);
        
        // Check vertex shader compilation
        int success;
        char infoLog[512];
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            std::cerr << "[START/FINISH] Vertex shader compilation failed:\n" << infoLog << std::endl;
            return 0;
        }
        
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &s_start_line_fragment_shader, NULL);
        glCompileShader(fragmentShader);
        
        // Check fragment shader compilation
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
            std::cerr << "[START/FINISH] Fragment shader compilation failed:\n" << infoLog << std::endl;
            glDeleteShader(vertexShader);
            return 0;
        }
        
        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        
        // Check linking
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
            std::cerr << "[START/FINISH] Shader program linking failed:\n" << infoLog << std::endl;
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return 0;
        }
        
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        
        std::cout << "[START/FINISH] Dedicated shader compiled successfully" << std::endl;
        return shaderProgram;
    }
    
    // Forward declaration of clearStartFinishLine (called in rebuildTrackCache)
    void clearStartFinishLine();
    
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
        // STEP 4: Initialize Start/Finish Line (ONCE per track load)
        // ========================================================================
        
        // Clear old line if exists
        clearStartFinishLine();
        
        // Compile dedicated shader
        s_start_line_shader = compileStartLineShader();
        if (s_start_line_shader == 0)
        {
            std::cerr << "[START/FINISH] Failed to compile shader!" << std::endl;
        }
        else
        {
            // Get first point position and direction
            const SplinePoint& firstPoint = smoothPoints[0];
            glm::vec2 startPos(firstPoint.position.x, firstPoint.position.y);
            glm::vec2 direction = glm::normalize(glm::vec2(firstPoint.tangent.x, firstPoint.tangent.y));
            glm::vec2 perpendicular(-direction.y, direction.x);
            
            // Line dimensions (100% track width)
            float lineWidth = TrackConstants::TRACK_ASPHALT_WIDTH * 1.0f;
            float lineLength = lineWidth * (5.2f / 8.6f);  // Its correct
            
            // Quad vertices with UV coordinates [x, y, u, v]
            float vertices[] = {
                // Bottom-left
                startPos.x - perpendicular.x * lineWidth/2.0f - direction.x * lineLength/2.0f,
                startPos.y - perpendicular.y * lineWidth/2.0f - direction.y * lineLength/2.0f,
                0.0f, 0.0f,
                // Bottom-right
                startPos.x + perpendicular.x * lineWidth/2.0f - direction.x * lineLength/2.0f,
                startPos.y + perpendicular.y * lineWidth/2.0f - direction.y * lineLength/2.0f,
                1.0f, 0.0f,
                // Top-right
                startPos.x + perpendicular.x * lineWidth/2.0f + direction.x * lineLength/2.0f,
                startPos.y + perpendicular.y * lineWidth/2.0f + direction.y * lineLength/2.0f,
                1.0f, 1.0f,
                // Top-left
                startPos.x - perpendicular.x * lineWidth/2.0f + direction.x * lineLength/2.0f,
                startPos.y - perpendicular.y * lineWidth/2.0f + direction.y * lineLength/2.0f,
                0.0f, 1.0f
            };
            
            glGenVertexArrays(1, &s_start_line_vao);
            glGenBuffers(1, &s_vbo_start_quad);
            
            glBindVertexArray(s_start_line_vao);
            glBindBuffer(GL_ARRAY_BUFFER, s_vbo_start_quad);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            
            // Position attribute (location 0)
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            
            // UV attribute (location 1)
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(1);
            
            glBindVertexArray(0);
            
            s_start_line_initialized = true;
            std::cout << "[START/FINISH] ✓ Line initialized at (" << startPos.x << ", " << startPos.y << ")" << std::endl;
            std::cout << "[START/FINISH]   Dimensions: width=" << lineWidth << ", length=" << lineLength << std::endl;
            
            // ========================================================================
            // Create gray start/finish line with rounded caps
            // ========================================================================
            s_debug_line.clear();
            
            // Line parameters (120% of BORDER width, NOT asphalt!)
            float lineHeight = TrackConstants::TRACK_BORDER_WIDTH * 1.2f;  // 120% from borders
            float lineThickness = 0.006f;               // Thicker so it's visible
            
            // Center rectangle (4 vertices)
            s_debug_line.push_back(glm::vec2(
                startPos.x - perpendicular.x * lineHeight/2.0f - direction.x * lineThickness/2.0f,
                startPos.y - perpendicular.y * lineHeight/2.0f - direction.y * lineThickness/2.0f
            ));
            s_debug_line.push_back(glm::vec2(
                startPos.x + perpendicular.x * lineHeight/2.0f - direction.x * lineThickness/2.0f,
                startPos.y + perpendicular.y * lineHeight/2.0f - direction.y * lineThickness/2.0f
            ));
            s_debug_line.push_back(glm::vec2(
                startPos.x - perpendicular.x * lineHeight/2.0f + direction.x * lineThickness/2.0f,
                startPos.y - perpendicular.y * lineHeight/2.0f + direction.y * lineThickness/2.0f
            ));
            s_debug_line.push_back(glm::vec2(
                startPos.x + perpendicular.x * lineHeight/2.0f + direction.x * lineThickness/2.0f,
                startPos.y + perpendicular.y * lineHeight/2.0f + direction.y * lineThickness/2.0f
            ));
            
            // Bottom rounded cap (semicircle - 8 segments)
            glm::vec2 bottomCenter = startPos - perpendicular * (lineHeight/2.0f);
            for (int i = 0; i <= 8; ++i)
            {
                float angle = 3.14159f + (i / 8.0f) * 3.14159f;  // 180° to 360° (bottom half)
                glm::vec2 offset(std::cos(angle) * lineThickness/2.0f, std::sin(angle) * lineThickness/2.0f);
                // Rotate offset by track direction
                glm::vec2 rotated(
                    offset.x * direction.x - offset.y * perpendicular.x,
                    offset.x * direction.y - offset.y * perpendicular.y
                );
                s_debug_line.push_back(bottomCenter + rotated);
            }
            
            // Top rounded cap (semicircle - 8 segments)
            glm::vec2 topCenter = startPos + perpendicular * (lineHeight/2.0f);
            for (int i = 0; i <= 8; ++i)
            {
                float angle = 0.0f + (i / 8.0f) * 3.14159f;  // 0° to 180° (top half)
                glm::vec2 offset(std::cos(angle) * lineThickness/2.0f, std::sin(angle) * lineThickness/2.0f);
                // Rotate offset by track direction
                glm::vec2 rotated(
                    offset.x * direction.x - offset.y * perpendicular.x,
                    offset.x * direction.y - offset.y * perpendicular.y
                );
                s_debug_line.push_back(topCenter + rotated);
            }
            
            std::cout << "[START/FINISH] Gray line created: " << lineHeight << " high × " << lineThickness << " thick (with rounded caps)" << std::endl;
            std::cout << "[START/FINISH]   Total vertices: " << s_debug_line.size() << std::endl;
        }
        
        // ========================================================================
        // STEP 5: Generate triangle strips
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
    
    // ============================================================================
    // Render gray start/finish line SEPARATELY (called AFTER checkered flag)
    // ============================================================================
    void renderStartFinishGrayLine(GLuint shader_program)
    {
        if (!s_track_cache_valid || s_track_vao == 0)
            return;
        
        // Cache uniform locations (reuse from renderCachedTrack)
        if (s_cached_shader != shader_program)
        {
            s_cached_colorLoc = glGetUniformLocation(shader_program, "uColor");
            s_cached_alphaLoc = glGetUniformLocation(shader_program, "uAlpha");
            s_cached_shader = shader_program;
        }
        
        // ========================================================================
        // Draw gray start/finish line with rounded caps (on top of checkered flag)
        // ========================================================================
        if (!s_debug_line.empty() && s_debug_line.size() >= 4)
        {
            glBindVertexArray(s_track_vao);
            
            // Upload line geometry
            std::vector<float> lineData;
            for (const auto& v : s_debug_line)
            {
                lineData.push_back(v.x);
                lineData.push_back(v.y);
            }
            
            GLuint tempVBO;
            glGenBuffers(1, &tempVBO);
            glBindBuffer(GL_ARRAY_BUFFER, tempVBO);
            glBufferData(GL_ARRAY_BUFFER, lineData.size() * sizeof(float), lineData.data(), GL_STREAM_DRAW);
            
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            
            glUniform3f(s_cached_colorLoc, 0.5f, 0.5f, 0.5f);  // Gray color
            glUniform1f(s_cached_alphaLoc, 1.0f);
            
            // Draw center rectangle (first 4 vertices)
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            
            // Draw bottom cap (next 9 vertices as triangle fan)
            if (s_debug_line.size() >= 13)
            {
                glDrawArrays(GL_TRIANGLE_FAN, 4, 9);
            }
            
            // Draw top cap (last 9 vertices as triangle fan)
            if (s_debug_line.size() >= 22)
            {
                glDrawArrays(GL_TRIANGLE_FAN, 13, 9);
            }
            
            glDeleteBuffers(1, &tempVBO);
            glBindVertexArray(0);
        }
    }
    
    bool isTrackCacheValid()
    {
        return s_track_cache_valid;
    }
    
    void clearTrackCache()
    {
        s_cached_border_layer.clear();
        s_cached_asphalt_layer.clear();
        s_debug_line.clear();
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
        
        // Clear Start/Finish Line (dedicated shader system)
        clearStartFinishLine();
        
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
    
    // ============================================================================
    // START/FINISH LINE RENDERING FUNCTIONS
    // ============================================================================
    
    void renderStartFinishLine(GLuint shader_program, const glm::mat4& viewProjection)
    {
        // Skip if not initialized (initialization moved to rebuildTrackCache)
        if (!s_start_line_initialized || s_start_line_vao == 0)
            return;
        
        // ========================================================================
        // RENDERING (Dedicated Shader + Depth Test Disabled)
        // ========================================================================
        
        // Use dedicated shader (NOT main shader!)
        glUseProgram(s_start_line_shader);
        
        // Set projection matrix
        GLint projLoc = glGetUniformLocation(s_start_line_shader, "projection");
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(viewProjection));
        
        // **CRITICAL**: Disable depth test to ensure line renders on top of asphalt
        GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        
        // Draw quad
        glBindVertexArray(s_start_line_vao);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glBindVertexArray(0);
        
        // Restore depth test state
        if (depthTestWasEnabled)
            glEnable(GL_DEPTH_TEST);
    }
    
    void clearStartFinishLine()
    {
        if (s_start_line_vao != 0)
        {
            glDeleteVertexArrays(1, &s_start_line_vao);
            glDeleteBuffers(1, &s_vbo_start_quad);
            s_start_line_vao = 0;
            s_vbo_start_quad = 0;
            s_start_line_initialized = false;
        }
        
        if (s_start_line_shader != 0)
        {
            glDeleteProgram(s_start_line_shader);
            s_start_line_shader = 0;
        }
        
        std::cout << "[START/FINISH] Line and shader cleared" << std::endl;
    }
}
