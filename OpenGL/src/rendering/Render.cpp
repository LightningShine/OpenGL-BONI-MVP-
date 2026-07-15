#include "Render.h"
#include "Interpolation.h"
#include "../Config.h"
#include "../vehicle/Vehicle.h"
#include "../input/Input.h"  //  g_is_map_loaded
#include "../racing/RaceManager.h"  // For RaceManager
#include <iostream>

// ============================================================================
// EXTERNAL GLOBALS 
// ============================================================================
extern std::atomic<bool> g_is_map_loaded;       // ?? Input.cpp
extern std::vector<SplinePoint> g_smooth_track_points;  // ?? main.cpp
extern RaceManager* g_race_manager;  // From main.cpp

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
        #version 330 core
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
        #version 330 core
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

    void rebuildTrackPreviewCache(const std::vector<glm::vec2>& points, std::mutex& points_mutex)
    {
        std::lock_guard<std::mutex> lock(points_mutex);
        if (points.size() <= 1)
        {
            s_cached_border_layer.clear();
            s_cached_asphalt_layer.clear();
            s_track_cache_valid = false;
            clearStartFinishLine();
            return;
        }

        // Preview: no filtering/simplification/smoothing to avoid artifacts for open polylines.
        std::vector<SplinePoint> spline;
        spline.reserve(points.size());
        for (size_t i = 0; i < points.size(); ++i)
        {
            SplinePoint sp;
            sp.position = points[i];
            if (i + 1 < points.size())
            {
                glm::vec2 d = points[i + 1] - points[i];
                if (glm::length(d) > 1e-6f)
                    sp.tangent = glm::normalize(d);
                else
                    sp.tangent = glm::vec2(1, 0);
            }
            else
            {
                sp.tangent = (i > 0) ? spline.back().tangent : glm::vec2(1, 0);
            }
            spline.push_back(sp);
        }

        clearStartFinishLine();
        s_start_line_initialized = false;

        s_cached_border_layer = generateTriangleStripFromLine(spline, TrackConstants::TRACK_BORDER_WIDTH);
        s_cached_asphalt_layer = generateTriangleStripFromLine(spline, TrackConstants::TRACK_ASPHALT_WIDTH);

        // Upload preview geometry to GPU (dynamic buffers)
        if (s_track_vao == 0)
        {
            glGenVertexArrays(1, &s_track_vao);
            glGenBuffers(1, &s_vbo_border);
            glGenBuffers(1, &s_vbo_asphalt);
        }

        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_border);
        glBufferData(GL_ARRAY_BUFFER,
            s_cached_border_layer.size() * sizeof(glm::vec2),
            s_cached_border_layer.data(),
            GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_asphalt);
        glBufferData(GL_ARRAY_BUFFER,
            s_cached_asphalt_layer.size() * sizeof(glm::vec2),
            s_cached_asphalt_layer.data(),
            GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        s_track_cache_valid = true;
    }
    
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
            
            std::cout << "[CACHE] Track cache cleared (no points)" << std::endl;
            return;
        }
        
        std::cout << "[CACHE] Rebuilding track cache with " << points.size() << " points..." << std::endl;
        
        // ========================================================================
        // STEP 1: Filter noise (??????? ????? ??????? ??????? ??????)
        // ========================================================================
        std::vector<glm::vec2> filteredPoints = filterPointsByDistance(points, 0.01f);
        std::cout << "[CACHE]   Step 1: Filtered to " << filteredPoints.size() << " points" << std::endl;
        
        // ========================================================================
        // STEP 2: Simplify path (??????? ??????????? ?? ?????? ????????)
        // ========================================================================
        std::vector<glm::vec2> simplifiedPoints = simplifyPath(filteredPoints, 0.005f);
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
            // SET RACE MANAGER START/FINISH LINE (for lap timing)
            // The line is perpendicular to the track direction at the first point
            // P1 = left edge, P2 = right edge (from track perspective)
            // ========================================================================
            if (g_race_manager)
            {
                glm::vec2 lineP1 = startPos - perpendicular * (lineWidth / 2.0f);
                glm::vec2 lineP2 = startPos + perpendicular * (lineWidth / 2.0f);
                 // Keep RaceManager start/finish line in the same coordinate space as Vehicle positions.
                 // In this project, vehicles are updated in the same (recentered) normalized space as the track.
                 g_race_manager->SetStartFinishLine(lineP1, lineP2);
            }
            
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

    void rebuildTrackCacheFromSplinePoints(const std::vector<SplinePoint>& smoothPoints)
    {
        if (smoothPoints.size() <= 1)
        {
            s_cached_border_layer.clear();
            s_cached_asphalt_layer.clear();
            s_track_cache_valid = false;
            g_is_map_loaded = false;

            std::cout << "[CACHE] Track cache cleared (no spline points)" << std::endl;
            return;
        }

        std::cout << "[CACHE] Rebuilding track cache from spline points (" << smoothPoints.size() << ")..." << std::endl;

        // Keep server-provided geometry/tangents as-is (important for consistent progress + start/finish line)
        g_smooth_track_points = smoothPoints;

        // ========================================================================
        // STEP 1: Initialize Start/Finish Line (ONCE per track load)
        // ========================================================================
        clearStartFinishLine();

        s_start_line_shader = compileStartLineShader();
        if (s_start_line_shader == 0)
        {
            std::cerr << "[START/FINISH] Failed to compile shader!" << std::endl;
        }
        else
        {
            const SplinePoint& firstPoint = smoothPoints[0];
            glm::vec2 startPos(firstPoint.position.x, firstPoint.position.y);

            glm::vec2 direction = glm::normalize(glm::vec2(firstPoint.tangent.x, firstPoint.tangent.y));
            if (glm::length(direction) < 1e-6f)
            {
                direction = glm::vec2(1.0f, 0.0f);
            }

            glm::vec2 perpendicular(-direction.y, direction.x);

            float lineWidth = TrackConstants::TRACK_ASPHALT_WIDTH * 1.0f;
            float lineLength = lineWidth * (5.2f / 8.6f);

            float vertices[] = {
                startPos.x - perpendicular.x * lineWidth/2.0f - direction.x * lineLength/2.0f,
                startPos.y - perpendicular.y * lineWidth/2.0f - direction.y * lineLength/2.0f,
                0.0f, 0.0f,

                startPos.x + perpendicular.x * lineWidth/2.0f - direction.x * lineLength/2.0f,
                startPos.y + perpendicular.y * lineWidth/2.0f - direction.y * lineLength/2.0f,
                1.0f, 0.0f,

                startPos.x + perpendicular.x * lineWidth/2.0f + direction.x * lineLength/2.0f,
                startPos.y + perpendicular.y * lineWidth/2.0f + direction.y * lineLength/2.0f,
                1.0f, 1.0f,

                startPos.x - perpendicular.x * lineWidth/2.0f + direction.x * lineLength/2.0f,
                startPos.y - perpendicular.y * lineWidth/2.0f + direction.y * lineLength/2.0f,
                0.0f, 1.0f
            };

            glGenVertexArrays(1, &s_start_line_vao);
            glGenBuffers(1, &s_vbo_start_quad);

            glBindVertexArray(s_start_line_vao);
            glBindBuffer(GL_ARRAY_BUFFER, s_vbo_start_quad);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);

            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(1);

            glBindVertexArray(0);

            s_start_line_initialized = true;

            if (g_race_manager)
            {
                glm::vec2 lineP1 = startPos - perpendicular * (lineWidth / 2.0f);
                glm::vec2 lineP2 = startPos + perpendicular * (lineWidth / 2.0f);
                g_race_manager->SetStartFinishLine(lineP1, lineP2);
            }

            // Recreate debug gray line (same logic as rebuildTrackCache)
            s_debug_line.clear();

            float lineHeight = TrackConstants::TRACK_BORDER_WIDTH * 1.2f;
            float lineThickness = 0.006f;

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

            glm::vec2 bottomCenter = startPos - perpendicular * (lineHeight/2.0f);
            for (int i = 0; i <= 8; ++i)
            {
                float angle = 3.14159f + (i / 8.0f) * 3.14159f;
                glm::vec2 offset(std::cos(angle) * lineThickness/2.0f, std::sin(angle) * lineThickness/2.0f);
                glm::vec2 rotated(
                    offset.x * direction.x - offset.y * perpendicular.x,
                    offset.x * direction.y - offset.y * perpendicular.y
                );
                s_debug_line.push_back(bottomCenter + rotated);
            }

            glm::vec2 topCenter = startPos + perpendicular * (lineHeight/2.0f);
            for (int i = 0; i <= 8; ++i)
            {
                float angle = 0.0f + (i / 8.0f) * 3.14159f;
                glm::vec2 offset(std::cos(angle) * lineThickness/2.0f, std::sin(angle) * lineThickness/2.0f);
                glm::vec2 rotated(
                    offset.x * direction.x - offset.y * perpendicular.x,
                    offset.x * direction.y - offset.y * perpendicular.y
                );
                s_debug_line.push_back(topCenter + rotated);
            }
        }

        // ========================================================================
        // STEP 2: Generate triangle strips + upload to GPU
        // ========================================================================
        s_cached_border_layer = generateTriangleStripFromLine(smoothPoints, TrackConstants::TRACK_BORDER_WIDTH);
        s_cached_asphalt_layer = generateTriangleStripFromLine(smoothPoints, TrackConstants::TRACK_ASPHALT_WIDTH);

        if (s_track_vao == 0)
        {
            glGenVertexArrays(1, &s_track_vao);
            glGenBuffers(1, &s_vbo_border);
            glGenBuffers(1, &s_vbo_asphalt);
            std::cout << "[CACHE]   Created OpenGL objects (VAO: " << s_track_vao
                      << ", VBO border: " << s_vbo_border
                      << ", VBO asphalt: " << s_vbo_asphalt << ")" << std::endl;
        }

        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_border);
        glBufferData(GL_ARRAY_BUFFER,
                     s_cached_border_layer.size() * sizeof(glm::vec2),
                     s_cached_border_layer.data(),
                     GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_asphalt);
        glBufferData(GL_ARRAY_BUFFER,
                     s_cached_asphalt_layer.size() * sizeof(glm::vec2),
                     s_cached_asphalt_layer.data(),
                     GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        s_track_cache_valid = true;
        g_is_map_loaded = true;

        std::cout << "[CACHE] ✓ Track uploaded to GPU (STATIC buffers, spline source)" << std::endl;
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
        
        glUniform3f(s_cached_colorLoc, 120.0f/255.0f, 120.0f / 255.0f, 120.0f / 255.0f);
        glUniform1f(s_cached_alphaLoc, 1.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(s_cached_border_layer.size()));
        
        // ========================================================================
        // Draw asphalt layer
        // ========================================================================
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_asphalt);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
        glEnableVertexAttribArray(0);
        
        glUniform3f(s_cached_colorLoc, 26.0f/255.0f, 26.0f / 255.0f, 26.0f / 255.0f);
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
        g_smooth_track_points.clear();  // PRO Track Map draws from these
        
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
    }

    // ── helpers ──────────────────────────────────────────────────────────────

    static std::pair<std::vector<glm::vec2>, std::vector<glm::vec2>>
    outsetEdges(const std::vector<glm::vec2>& left,
                const std::vector<glm::vec2>& right, float amount)
    {
        const size_t n = left.size() < right.size() ? left.size() : right.size();
        std::vector<glm::vec2> ol(n), orr(n);
        for (size_t i = 0; i < n; ++i)
        {
            glm::vec2 d = right[i] - left[i];
            float len = glm::length(d);
            if (len < 1e-6f) { ol[i] = left[i]; orr[i] = right[i]; continue; }
            glm::vec2 u = d / len;
            ol[i]  = left[i]  - u * amount;
            orr[i] = right[i] + u * amount;
        }
        return {ol, orr};
    }

    static void uploadEdgeGeometry(
        const std::vector<glm::vec2>& border,
        const std::vector<glm::vec2>& asphalt,
        GLenum usage)
    {
        if (s_track_vao == 0)
        {
            glGenVertexArrays(1, &s_track_vao);
            glGenBuffers(1, &s_vbo_border);
            glGenBuffers(1, &s_vbo_asphalt);
        }
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_border);
        glBufferData(GL_ARRAY_BUFFER, border.size() * sizeof(glm::vec2), border.data(), usage);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_asphalt);
        glBufferData(GL_ARRAY_BUFFER, asphalt.size() * sizeof(glm::vec2), asphalt.data(), usage);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // Initialise the start/finish line given the two edge endpoints.
    // p1 = left edge point at start, p2 = right edge point at start.
    static void setupStartFinishFromEdgePoints(const glm::vec2& p1, const glm::vec2& p2)
    {
        glm::vec2 across  = p2 - p1;
        float lineWidth   = glm::length(across);
        if (lineWidth < 1e-6f) return;

        glm::vec2 perp    = across / lineWidth;                   // unit: p1→p2
        glm::vec2 startPos = (p1 + p2) * 0.5f;
        glm::vec2 dir(-perp.y, perp.x);                          // along track

        float lineLength  = lineWidth * (5.2f / 8.6f);

        clearStartFinishLine();
        s_start_line_shader = compileStartLineShader();
        if (!s_start_line_shader) return;

        float verts[] = {
            startPos.x - perp.x * lineWidth/2 - dir.x * lineLength/2,
            startPos.y - perp.y * lineWidth/2 - dir.y * lineLength/2, 0.f, 0.f,
            startPos.x + perp.x * lineWidth/2 - dir.x * lineLength/2,
            startPos.y + perp.y * lineWidth/2 - dir.y * lineLength/2, 1.f, 0.f,
            startPos.x + perp.x * lineWidth/2 + dir.x * lineLength/2,
            startPos.y + perp.y * lineWidth/2 + dir.y * lineLength/2, 1.f, 1.f,
            startPos.x - perp.x * lineWidth/2 + dir.x * lineLength/2,
            startPos.y - perp.y * lineWidth/2 + dir.y * lineLength/2, 0.f, 1.f,
        };

        glGenVertexArrays(1, &s_start_line_vao);
        glGenBuffers(1, &s_vbo_start_quad);
        glBindVertexArray(s_start_line_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo_start_quad);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        s_start_line_initialized = true;

        // Gray line
        s_debug_line.clear();
        float lh = TrackConstants::TRACK_BORDER_WIDTH * 1.2f, lt = 0.006f;
        s_debug_line.push_back(startPos - perp*(lh/2) - dir*(lt/2));
        s_debug_line.push_back(startPos + perp*(lh/2) - dir*(lt/2));
        s_debug_line.push_back(startPos - perp*(lh/2) + dir*(lt/2));
        s_debug_line.push_back(startPos + perp*(lh/2) + dir*(lt/2));
        for (int i = 0; i <= 8; ++i) {
            float a = 3.14159f + i/8.f * 3.14159f;
            glm::vec2 o(std::cos(a)*lt/2, std::sin(a)*lt/2);
            glm::vec2 r(o.x*dir.x - o.y*perp.x, o.x*dir.y - o.y*perp.y);
            s_debug_line.push_back(startPos - perp*(lh/2) + r);
        }
        for (int i = 0; i <= 8; ++i) {
            float a = i/8.f * 3.14159f;
            glm::vec2 o(std::cos(a)*lt/2, std::sin(a)*lt/2);
            glm::vec2 r(o.x*dir.x - o.y*perp.x, o.x*dir.y - o.y*perp.y);
            s_debug_line.push_back(startPos + perp*(lh/2) + r);
        }

        if (g_race_manager)
            g_race_manager->SetStartFinishLine(p1, p2);
    }

    // ── New dual-edge functions ───────────────────────────────────────────────

    void rebuildTrackCacheFromEdges(
        const std::vector<glm::vec2>& leftIn,
        const std::vector<glm::vec2>& rightIn)
    {
        if (leftIn.size() < 2 || rightIn.size() < 2) return;

        // Compute centroid of the centre line and apply centering offset so the
        // track appears at the screen centre (same behaviour as the TXT-path's
        // recenterTrack).  g_track_render_offset is set so vehicle positions
        // rendered from GPS telemetry are shifted by the same amount.
        const size_t n = leftIn.size() < rightIn.size() ? leftIn.size() : rightIn.size();
        glm::vec2 sum(0.f, 0.f);
        for (size_t i = 0; i < n; ++i)
            sum += (leftIn[i] + rightIn[i]) * 0.5f;
        const glm::vec2 offset = -(sum / (float)n);

        std::vector<glm::vec2> left(n), right(n), centres(n);
        for (size_t i = 0; i < n; ++i) {
            left[i]    = leftIn[i]    + offset;
            right[i]   = rightIn[i]   + offset;
            centres[i] = (left[i] + right[i]) * 0.5f;
        }
        g_track_render_offset = offset;

        auto [bL, bR] = outsetEdges(left, right, 0.015f);
        s_cached_border_layer  = generateTriangleStripFromEdges(bL, bR);
        s_cached_asphalt_layer = generateTriangleStripFromEdges(left, right);

        g_smooth_track_points = interpolatePointsWithTangents(centres, 6);

        setupStartFinishFromEdgePoints(left[0], right[0]);
        uploadEdgeGeometry(s_cached_border_layer, s_cached_asphalt_layer, GL_STATIC_DRAW);

        s_track_cache_valid = true;
        g_is_map_loaded     = true;
        std::cout << "[CACHE] Track (dual-edge) uploaded, centered offset=("
                  << offset.x << "," << offset.y << ")\n";
    }

    void rebuildDualEdgePreviewCache(
        const std::vector<glm::vec2>& left,
        const std::vector<glm::vec2>& right)
    {
        if (left.size() < 2 || right.size() < 2) return;

        s_cached_asphalt_layer = generateTriangleStripFromEdges(left, right);
        auto [bL, bR] = outsetEdges(left, right, 0.010f);
        s_cached_border_layer  = generateTriangleStripFromEdges(bL, bR);

        uploadEdgeGeometry(s_cached_border_layer, s_cached_asphalt_layer, GL_DYNAMIC_DRAW);
        s_track_cache_valid = true;
    }

    void rebuildEdgeLineCache(const std::vector<glm::vec2>& pts)
    {
        if (pts.size() < 2) { s_track_cache_valid = false; return; }

        std::vector<SplinePoint> spline;
        spline.reserve(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) {
            SplinePoint sp;
            sp.position = pts[i];
            if (i + 1 < pts.size()) {
                glm::vec2 d = pts[i + 1] - pts[i];
                sp.tangent = (glm::length(d) > 1e-6f) ? glm::normalize(d) : glm::vec2(1.f, 0.f);
            } else {
                sp.tangent = (i > 0) ? spline.back().tangent : glm::vec2(1.f, 0.f);
            }
            spline.push_back(sp);
        }

        // Narrow strips: just enough to be visible as a single edge line
        s_cached_border_layer  = generateTriangleStripFromLine(spline, 0.007f);
        s_cached_asphalt_layer = generateTriangleStripFromLine(spline, 0.003f);
        uploadEdgeGeometry(s_cached_border_layer, s_cached_asphalt_layer, GL_DYNAMIC_DRAW);
        s_track_cache_valid = true;
    }
}
