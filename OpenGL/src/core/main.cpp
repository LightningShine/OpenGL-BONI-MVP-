#pragma once
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <fstream>


// === WINDOWS BORDER COLOR ===
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>		// For glfwGetWin32Window
#include <dwmapi.h>					// DwmSetWindowAttribute function
#pragma comment(lib, "dwmapi.lib")  // Link with dwmapi.lib

// =================================
// === 3RD LIBRARIES ===
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// =================================
// === PROJECTS FILES ===
#include "../Config.h"
#include "../ui/UI_Config.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../rendering/Render.h"          
#include "../../UI.h"
#include "../network/Server.h"
#include "../network/Client.h"
#include "../network/ESP32_Code.h"
#include "../vehicle/Vehicle.h"


using namespace std;


struct AppContext {
    float* zoom;
    std::vector<glm::vec2>* points;
    std::mutex* points_mutex;
    UI* ui;
};

// ============================================================================
// GLOBAL VARIABLES FOR TRACK SIMULATION
// ============================================================================
// Smooth track points used for vehicle simulation (generated from raw track)
std::vector<SplinePoint> g_smooth_track_points;


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window, glm::vec2& camera_pos, float& zoom, float& rotation, float speed, 
const std::vector<SplinePoint>* smooth_track = nullptr)
{
	// Close when press ESC
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
	bool isServerKeyPressed = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

	if (isServerKeyPressed)
	{
		if (!isServerRunning())
		{
			continueServerRunning();
			thread ServerThread = thread(serverWork);
			ServerThread.detach();
			ChangeisServerRunning();
		}
		else
		{
			serverStop();
			ChangeisServerRunning();
		}
		
	}


	bool isClientKeyPressed = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
	if (isClientKeyPressed)
	{
		if (!isClientRunning())
		{
			continueClientRunning();
			thread ClientThread = thread(clientStart);
			ClientThread.detach();
			toggleClientRunning();
		}
		else
		{
			clientStop();
			toggleClientRunning();
		}
	}
	

	static bool wasTPressed = false;
	if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !wasTPressed)
	{
		wasTPressed = true;
		
		// Guard clauses
		if (!g_is_map_loaded) {
			std::cout << "Cannot create vehicle - map not loaded!" << std::endl;
		} else if (smooth_track->empty()) {
			std::cout << "Cannot create vehicle - track not interpolated!" << std::endl;
		} else {
			// Create vehicle at origin
			Vehicle new_vehicle;
			int vehicle_id = new_vehicle.m_id;
			
			{
				std::lock_guard<std::mutex> lock(g_vehicles_mutex);
				g_vehicles[vehicle_id] = new_vehicle;
			}
			
			std::cout << "Vehicle #" << vehicle_id << " created - starting simulation" << std::endl;
			
			// Start automatic simulation along track
			simulateVehicleMovement(vehicle_id, *smooth_track);
		}
	}
	if (glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE)
	{
		wasTPressed = false;
	}


	// Camera movement (W/A/S/D or Arrows)
	// Note: Arrow keys are used for rotation when R is pressed
	bool isRotationMode = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
	
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera_pos.y += speed / zoom;  // Up

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera_pos.y -= speed / zoom;  // Down

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera_pos.x -= speed / zoom;  // Left

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera_pos.x += speed / zoom;  // Right
	
	// Arrow keys for movement (only if R is not pressed)
	if (!isRotationMode)
	{
		if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
			camera_pos.y += speed / zoom;
		
		if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
			camera_pos.y -= speed / zoom;
		
		if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
			camera_pos.x -= speed / zoom;
		
		if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
			camera_pos.x += speed / zoom;
	}
	
	// Camera rotation (R + Left/Right arrows)
	if (isRotationMode)
	{
		if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		{
			rotation -= CameraConstants::CAMERA_ROTATION_SPEED;
			if (rotation < CameraConstants::CAMERA_ROTATION_MIN)
				rotation = CameraConstants::CAMERA_ROTATION_MIN;
		}
		
		if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		{
			rotation += CameraConstants::CAMERA_ROTATION_SPEED;
			if (rotation > CameraConstants::CAMERA_ROTATION_MAX)
				rotation = CameraConstants::CAMERA_ROTATION_MAX;
		}
	}

	// Zoom limits
	if (zoom < CameraConstants::CAMERA_ZOOM_MIN) zoom = CameraConstants::CAMERA_ZOOM_MIN;
	if (zoom > CameraConstants::CAMERA_ZOOM_MAX) zoom = CameraConstants::CAMERA_ZOOM_MAX;


	static bool was_f11_pressed = false;
	static bool is_fullscreen = false;
	
	if (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS && !was_f11_pressed)
	{
		was_f11_pressed = true;
		
		if (!is_fullscreen)
		{
			// Go fullscreen (maximized)
			glfwMaximizeWindow(window);
			is_fullscreen = true;
		}
		else
		{
			// Restore to 500x500 centered window
			glfwRestoreWindow(window);
			
			// Center 500x500 window on screen
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);
			int center_x = (mode->width - 500) / 2;
			int center_y = (mode->height - 500) / 2;
			
			glfwSetWindowSize(window, 500, 500);
			glfwSetWindowPos(window, center_x, center_y);
			is_fullscreen = false;
		}
	}
	if (glfwGetKey(window, GLFW_KEY_F11) == GLFW_RELEASE)
	{
		was_f11_pressed = false;
	}
}

// Callback for scroolling mouse wheel
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	AppContext* context = (AppContext*)glfwGetWindowUserPointer(window);
	if (context && context->zoom) {
		float* zoom = context->zoom;
		*zoom *= (float)(1.0f + yoffset * 0.1f);  // Aspect of zoom change

		if (*zoom < CameraConstants::CAMERA_ZOOM_MIN) *zoom = CameraConstants::CAMERA_ZOOM_MIN;
		if (*zoom > CameraConstants::CAMERA_ZOOM_MAX) *zoom = CameraConstants::CAMERA_ZOOM_MAX;
	}
}

void drop_callback(GLFWwindow* window, int count, const char** paths)
{
	AppContext* context = (AppContext*)glfwGetWindowUserPointer(window);
	if (context && context->points && context->points_mutex)
	{
		for (int i = 0; i < count; i++)
		{
			std::ifstream file(paths[i]);
			if (file.is_open())
			{
				std::stringstream buffer;
				buffer << file.rdbuf();
				loadTrackFromData(buffer.str(), *context->points, *context->points_mutex);
				std::cout << "Loaded file: " << paths[i] << std::endl;
                
				// Recenter track to (0, 0) if closed
				{
					std::lock_guard<std::mutex> lock(*context->points_mutex);
					TrackCenterInfo center_info = calculateTrackCenter(*context->points);
					
					if (center_info.is_closed) {
						std::cout << "[TRACK] Track is CLOSED - recentering to (0, 0)" << std::endl;
						recenterTrack(*context->points, center_info);
						
						// Update origin to maintain GPS coordinates
						g_map_origin.m_origin_lat_dd += center_info.offset.y * (MapConstants::MAP_SIZE / 100000.0);
						g_map_origin.m_origin_lon_dd += center_info.offset.x * (MapConstants::MAP_SIZE / 100000.0);
						std::cout << "[TRACK] Origin updated to: (" << g_map_origin.m_origin_lat_dd << ", " << g_map_origin.m_origin_lon_dd << ")" << std::endl;
					} else {
						std::cout << "[TRACK] Track is OPEN - keeping original position" << std::endl;
					}
				}
				
				TrackRenderer::rebuildTrackCache(*context->points, *context->points_mutex);
				
                if (context->ui)
                {
                    context->ui->CloseSplash();
                }
			}
			else
			{
				std::cerr << "Failed to open file: " << paths[i] << std::endl;
			}
		}
	}
}


//Shaders

const char* vertexShaderSource = R"(
    #version 460 core
    layout (location = 0) in vec2 aPos;
    
    uniform mat4 projection;
    
    void main()
    {
        gl_Position = projection * vec4(aPos.x, aPos.y, 0.0, 1.0); 
    }
)";

const char* fragmentShaderSource = R"(
    #version 460 core
    out vec4 FragColor;
    uniform vec3 uColor;
    uniform float uAlpha;
    
    void main()
    {
        FragColor = vec4(uColor, uAlpha); 
    }
)";

// Render grid function
void renderGrid(GLuint shader_program, GLuint grid_vao, GLuint grid_vbo,
glm::vec2 camera_position, float camera_zoom, float camera_rotation,
int window_width, int window_height, float horizontalBound, float verticalBound)
{
    // Calculate view bounds in world coordinates
    float view_width = MapConstants::MAP_BOUND_X * 2.0f / camera_zoom;
    float view_height = MapConstants::MAP_BOUND_Y * 2.0f / camera_zoom;
    
    // Expand bounds to account for rotation (prevent artifacts at corners)
    float rotation_rad = glm::radians(std::abs(camera_rotation));
    float expansion_factor = 1.0f + std::sin(rotation_rad) * 0.5f;  // Max ~1.5x at 90 degrees
    view_width *= expansion_factor;
    view_height *= expansion_factor;
    
    float world_left = camera_position.x - view_width / 2.0f;
    float world_right = camera_position.x + view_width / 2.0f;
    float world_bottom = camera_position.y - view_height / 2.0f;
    float world_top = camera_position.y + view_height / 2.0f;
    
    // Convert grid cell size to OpenGL coordinates
    float cell_size_opengl = GridConstants::GRID_CELL_SIZE / MapConstants::MAP_SIZE;
    
    // Calculate grid start/end aligned to cell_size
    float grid_start_x = std::floor(world_left / cell_size_opengl) * cell_size_opengl;
    float grid_end_x = std::ceil(world_right / cell_size_opengl) * cell_size_opengl;
    float grid_start_y = std::floor(world_bottom / cell_size_opengl) * cell_size_opengl;
    float grid_end_y = std::ceil(world_top / cell_size_opengl) * cell_size_opengl;
    
    // Prepare line vertices
    std::vector<float> grid_vertices;
    
    // Vertical lines
    for (float x = grid_start_x; x <= grid_end_x; x += cell_size_opengl)
    {
        grid_vertices.push_back(x);
        grid_vertices.push_back(grid_start_y);
        
        grid_vertices.push_back(x);
        grid_vertices.push_back(grid_end_y);
    }
    
    // Horizontal lines
    for (float y = grid_start_y; y <= grid_end_y; y += cell_size_opengl)
    {
        grid_vertices.push_back(grid_start_x);
        grid_vertices.push_back(y);
        
        grid_vertices.push_back(grid_end_x);
        grid_vertices.push_back(y);
    }
    
    
    if (grid_vertices.empty()) return;
    
    // Bind existing VAO/VBO (created ONCE during initialization)
    glBindVertexArray(grid_vao);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, grid_vertices.size() * sizeof(float), grid_vertices.data(), GL_DYNAMIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Draw grid with transparency
    glUseProgram(shader_program);
    
    // Set projection matrix with correct aspect ratio
    GLint proj_loc = glGetUniformLocation(shader_program, "projection");
    
    float zoomedHorizontal = horizontalBound / camera_zoom;
    float zoomedVertical = verticalBound / camera_zoom;
    
    glm::mat4 projection = glm::ortho(
        -zoomedHorizontal,  // left
        zoomedHorizontal,   // right
        -zoomedVertical,    // bottom
        zoomedVertical,     // top
        -1.0f, 1.0f
    );
    
    // Grid is STATIC - no rotation, only position
    glm::mat4 view = glm::mat4(1.0f);
    view = glm::translate(view, glm::vec3(-camera_position.x, -camera_position.y, 0.0f));
    glm::mat4 viewProjection = projection * view;
    glUniformMatrix4fv(proj_loc, 1, GL_FALSE, glm::value_ptr(viewProjection));
    
    GLint color_loc = glGetUniformLocation(shader_program, "uColor");
    GLint alpha_loc = glGetUniformLocation(shader_program, "uAlpha");
    
    glUniform3f(color_loc, 
                GridConstants::GRID_COLOR_R, 
                GridConstants::GRID_COLOR_G, 
                GridConstants::GRID_COLOR_B);
    glUniform1f(alpha_loc, GridConstants::GRID_LINE_ALPHA);
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Use line width 1.0
    glLineWidth(1.0f);
    
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(grid_vertices.size() / 2));
    
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

int main()
{
	std::cout << "[MAIN] Starting application..." << std::endl;
	
	glfwInit();
	std::cout << "[MAIN] GLFW initialized" << std::endl;
	
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);  // Determine OpenGL major version OpenGL 4.X
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);	// Determine OpenGL minor version OpenGL X.6
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	// Use the core-profile
	// get access to a et access to a smaller subset 
	// of OpenGL features without backwards - compatible features we no longer need

	int Width;
	int Height;

	std::cout << "[MAIN] Getting primary monitor..." << std::endl;
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	
	if (!monitor)
	{
		std::cerr << "[ERROR] Failed to get primary monitor!" << std::endl;
		glfwTerminate();
		return -1;
	}
	
	std::cout << "[MAIN] Getting video mode..." << std::endl;
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);
	
	if (!mode)
	{
		std::cerr << "[ERROR] Failed to get video mode!" << std::endl;
		glfwTerminate();
		return -1;
	}
	
	Width = mode->width;
	Height = mode->height;
	
	std::cout << "[MAIN] Screen resolution: " << Width << "x" << Height << std::endl;

	//glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); //Full mode without borders
    // Use NULL for monitor to create a windowed mode (borderless because of GLFW_DECORATED = FALSE)
    // This fixes the black screen issue on capture and cursor visibility
	glfwWindowHint(GLFW_SAMPLES, 4);
	
	std::cout << "[MAIN] Creating window " << Width << "x" << Height << "..." << std::endl;
	GLFWwindow* window = glfwCreateWindow(Width, Height, UIConfig::APP_NAME, NULL, NULL);
	
	std::cout << "[MAIN] Window created, enabling multisampling..." << std::endl;
	
	if (window == NULL)
	{
		cout << "[ERROR] Failed to create GLFW window" << endl;
		glfwTerminate();
		return -1;
	}
	
	std::cout << "[MAIN] Window created successfully" << std::endl;

	
	
	// === CHANGE WINDOW BORDER COLOR (Windows 11) ===
	HWND hwnd = glfwGetWin32Window(window);

	// Color in the format 0x00BBGGRR (Blue, Green, Red)
	// #181818 = RGB(24, 24, 24) = 0x00181818
	COLORREF titleBarColor = 0x00181818; // Match menu background color
	COLORREF borderColor = 0x00181818;   // Border color

	// DWMWA_CAPTION_COLOR = 35, DWMWA_BORDER_COLOR = 34
	DwmSetWindowAttribute(hwnd, 35, &titleBarColor, sizeof(titleBarColor));
	DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));

	// Force dark title bar theme
	BOOL useDarkMode = TRUE;
	DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode)); // DWMWA_USE_IMMERSIVE_DARK_MODE


	glfwMakeContextCurrent(window); // Make the window's context in current thread
	std::cout << "[MAIN] Made context current" << std::endl;
	
	glfwSwapInterval(1);  // Enable V-Sync (lock FPS to monitor refresh rate, e.g. 60 Hz)
	std::cout << "[MAIN] V-Sync enabled" << std::endl;
	
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // Show the cursor
	std::cout << "[MAIN] Cursor mode set" << std::endl;

	std::cout << "[MAIN] Context created, loading GLAD..." << std::endl;

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) // Initialize GLAD before calling any OpenGL function
	{
		cout << "Failed to initialize GLAD" << endl;
		return -1;
	}
	// Commit
	std::cout << "[MAIN] GLAD loaded successfully" << std::endl;
	
	glEnable(GL_MULTISAMPLE);
	std::cout << "[MAIN] Multisampling enabled" << std::endl;

	glViewport(0, 0, Width, Height); // Set the OpenGL viewport to cover the whole window
	std::cout << "[MAIN] Viewport set" << std::endl;

	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	// Register the callback function to adjust the viewport when the window is resized

	glfwMaximizeWindow(window);


	GLuint vao, vbo, EBO;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &EBO);

	// Vertex Buffer Object
	glBindVertexArray(vao);
	
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	//Index Buffer Object
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

	//Vertex Attribute
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);

	// ================= UI Initialization ==================



	std::cout << "[MAIN] Initializing UI..." << std::endl;
	
	UI ui;
	if (!ui.Initialize(window))
	{
		std::cerr << "Failed UI Initialization" << endl;
		glfwTerminate();
		return -1;
	}
	
	std::cout << "[MAIN] UI initialized successfully" << std::endl;



	// ================= Shader compilation ===================
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);

	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);


	// Shader compile check

	int success;
	char infoLog[512];

	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
		cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << endl;
	}

	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
		cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << endl;
	}

	// =================== Create Shader Program ===================

	GLuint shader_program = glCreateProgram();

	glAttachShader(shader_program, vertexShader);
	glAttachShader(shader_program, fragmentShader);

	glLinkProgram(shader_program);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	glEnable(GL_PROGRAM_POINT_SIZE);

	// ========================== Process Input ==========================
	glm::vec2 camera_position(0.0f, 0.0f);
	float camera_zoom = 1.0f;
	float camera_rotation = 0.0f;  // Camera rotation in degrees (-89 to +89)
	float camera_move_speed = CameraConstants::CAMERA_MOVE_SPEED;
	glm::vec2 camera_velocity(0.0f, 0.0f);
	float friction = CameraConstants::CAMERA_FRICTION;
	float map_bound_x = MapConstants::MAP_BOUND_X;
	float map_bound_y = MapConstants::MAP_BOUND_Y;

	std::vector<glm::vec2> points;
	std::mutex  points_mutex;
	
	// Store smooth track for vehicle simulation
	// ???????: std::vector<SplinePoint> g_smooth_track_points; - ?????????? ?????????? ?????? (?????? 48)

	AppContext appContext;
	appContext.zoom = &camera_zoom;
	appContext.points = &points;
	appContext.points_mutex = &points_mutex;
    appContext.ui = &ui;

	glfwSetWindowUserPointer(window, &appContext);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetDropCallback(window, drop_callback);


	// ========================== INPUT ==========================
	
	ui.SetTrackData(&points, &points_mutex);

	int windowswidth;
	int windowsheight;

	double mapRange = 1.0; // Assuming the map range is normalized to 1.0

	double horizontalBound = mapRange;
	double verticalBound = mapRange;

	// ========================== VEHICLE SYSTEM INITIALIZATION ==========================
	
	std::thread vehicleThread(vehicleLoop);
	vehicleThread.detach();
	std::cout << "vehicleLoop thread started" << std::endl;




























































	// ========================== GRID VAO/VBO INITIALIZATION ==========================
	// Create grid VAO/VBO ONCE before render loop (NOT every frame!)
	GLuint grid_vao, grid_vbo;
	glGenVertexArrays(1, &grid_vao);
	glGenBuffers(1, &grid_vbo);
	std::cout << "Grid VAO/VBO created (VAO: " << grid_vao << ", VBO: " << grid_vbo << ")" << std::endl;

	// ========================== RENDER LOOP ==========================

	while (!glfwWindowShouldClose(window)) // Main loop that runs until the window is closed
	{
		ui.BeginFrame();

		processInput(window, camera_position, camera_zoom, camera_rotation, camera_move_speed, &g_smooth_track_points);
		camera_position += camera_velocity;  
		camera_velocity *= friction;
		
		glClearColor(26.0f / 255.0f, 26.0f / 255.0f, 26.0f / 255.0f, 1.0f); // Set the clear color for the window (background color)
		glClear(GL_COLOR_BUFFER_BIT); // Clear the color buffer with the specified clear color


		{
			glfwGetWindowSize(window, &windowswidth, &windowsheight);

			double aspectRatio = (double)windowswidth / (double)windowsheight;


			// Basic border with aspect ratio
			if (aspectRatio >= 1.0)
			{
				horizontalBound = mapRange * aspectRatio;
				verticalBound = mapRange;
			}
			else
			{
				horizontalBound = mapRange;
				verticalBound = mapRange / aspectRatio;
			}
		}
		
		// Apply zoom (devide border on zoom)
		float zoomedHorizontal = (float)horizontalBound / (float)camera_zoom;
		float zoomedVertical = (float)verticalBound / (float)camera_zoom;

		camera_position.x = glm::clamp(camera_position.x, -map_bound_x, map_bound_x);
		camera_position.y = glm::clamp(camera_position.y, -map_bound_y, map_bound_y);

		// Creating orthographic projection matrix
		glm::mat4 projection = glm::ortho(
			-zoomedHorizontal,  // left
			zoomedHorizontal,   // right
			-zoomedVertical,    // bottom
			zoomedVertical,     // top
			-1.0f, 1.0f
		);
		
		// Creating view matrix with camera position and rotation
		glm::mat4 view_world = glm::mat4(1.0f); glm::mat4 view_grid = glm::mat4(1.0f);
	view_world = glm::translate(view_world, glm::vec3(-camera_position.x, -camera_position.y, 0.0f));
	view_world = glm::rotate(view_world, glm::radians(camera_rotation), glm::vec3(0.0f, 0.0f, 1.0f));
	view_grid = glm::translate(view_grid, glm::vec3(-camera_position.x, -camera_position.y, 0.0f));
		
	
	glm::mat4 viewProjection_world = projection * view_world;
	glm::mat4 viewProjection_grid = projection * view_grid;

		// Track creation and drawing
		glUseProgram(shader_program);

	GLint projLoc = glGetUniformLocation(shader_program, "projection");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(viewProjection_world));
		

		GLint colorLoc = glGetUniformLocation(shader_program, "uColor");
		GLint alphaLoc = glGetUniformLocation(shader_program, "uAlpha");

		glBindVertexArray(vao);
		
	// Render grid (always, even without track)
	renderGrid(shader_program, grid_vao, grid_vbo, camera_position, camera_zoom, camera_rotation, Width, Height, 
	           (float)horizontalBound, (float)verticalBound);
		
	
	// Track rendering from GPU cache
	glUseProgram(shader_program);
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(viewProjection_world));
	TrackRenderer::renderCachedTrack(shader_program);




	if (g_is_map_loaded) { 
		renderAllVehicles(shader_program, vao, vbo, viewProjection_world, camera_position, camera_zoom); 
	}

	// ========================== UI RENDERING ==========================
		// Render UI AFTER track and vehicles so it's on top
		ui.Render();
		ui.EndFrame();
		
		// check and call events and swap the buffers
		 
		glfwPollEvents(); // Poll for and process events (e.g., keyboard, mouse)
		glfwSwapBuffers(window); // Swap the front and back buffers (render image display)
		
	}
	

	// ========================== CLEAN UP ==========================
	ui.Shutdown();

	serverStop();
	clientStop();
	TrackRenderer::clearTrackCache();  // Clear track VAO/VBO
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &grid_vao);  // Clean grid VAO/VBO
	glDeleteBuffers(1, &grid_vbo);
	glDeleteProgram(shader_program);
	glfwTerminate(); // Clean up all resources allocated by GLFW and exit

	return 0;

	// Uztaisit Start Stop sistemu lai pec N laiku MVP nepardota informacija, lai taupitu energiju

}
