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
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
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

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window, glm::vec2& camera_pos, float& zoom, float speed, 
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


	// Camera movment (W/A/S/D or Arrows)
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		camera_pos.y += speed / zoom;  // Up

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		camera_pos.y -= speed / zoom;  // down

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		camera_pos.x -= speed / zoom;  // Left

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		camera_pos.x += speed / zoom;  // Right

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
    
    void main()
    {
        FragColor = vec4(uColor, 1.0f); 
    }
)";


int main()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);  // Determine OpenGL major version OpenGL 4.X
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);	// Determine OpenGL minor version OpenGL X.6
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	// Use the core-profile
	// get access to a et access to a smaller subset 
	// of OpenGL features without backwards - compatible features we no longer need

	int Width;
	int Height;

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();

	const GLFWvidmode* mode = glfwGetVideoMode(monitor);
	Width = mode->width;
	Height = mode->height;

	//glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); //Full mode without borders
    // Use NULL for monitor to create a windowed mode (borderless because of GLFW_DECORATED = FALSE)
    // This fixes the black screen issue on capture and cursor visibility
	GLFWwindow* window = glfwCreateWindow(Width, Height, "RACE APP", NULL, NULL); 
	if (window == NULL)
	{
		cout << "Failed to create GLFW window" << endl;
		glfwTerminate();
		return -1;
	}

	
	
	// === CHANGE WINDOW BORDER COLOR (Windows 11) ===
	HWND hwnd = glfwGetWin32Window(window);

	// Color in the format 0x00BBGGRR (Blue, Green, Red)
	COLORREF titleBarColor = 0x00262626; // Dark gray (almost black)
	COLORREF borderColor = 0x00262626;   // Border color

	// DWMWA_CAPTION_COLOR = 35, DWMWA_BORDER_COLOR = 34
	DwmSetWindowAttribute(hwnd, 35, &titleBarColor, sizeof(titleBarColor));
	DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));

	// Force dark title bar theme
	BOOL useDarkMode = TRUE;
	DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode)); // DWMWA_USE_IMMERSIVE_DARK_MODE

	glfwMakeContextCurrent(window); // Make the window's context in current thread
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // Show the cursor

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) // Initialize GLAD before calling any OpenGL function
	{
		cout << "Failed to initialize GLAD" << endl;
		return -1;
	}

	glViewport(0, 0, Width, Height); // Set the OpenGL viewport to cover the whole window

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

	glfwMakeContextCurrent(window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	UI ui;
	if (!ui.Initialize(window))
	{
		std::cerr << "Failed UI Initialization" << endl;
		glfwTerminate();
		return -1;
	}



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
	float camera_move_speed = CameraConstants::CAMERA_MOVE_SPEED;
	glm::vec2 camera_velocity(0.0f, 0.0f);
	float friction = CameraConstants::CAMERA_FRICTION;
	float map_bound_x = MapConstants::MAP_BOUND_X;
	float map_bound_y = MapConstants::MAP_BOUND_Y;

	std::vector<glm::vec2> points;
	std::mutex  points_mutex;
	
	// Store smooth track for vehicle simulation
	std::vector<SplinePoint> g_smooth_track_points;

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
	
	// ✅ Запускаем фоновый поток vehicleLoop для обслуживания машин
	std::thread vehicleThread(vehicleLoop);
	vehicleThread.detach();
	std::cout << "vehicleLoop thread started" << std::endl;

	// ========================== RENDER LOOP ==========================

	while (!glfwWindowShouldClose(window)) // Main loop that runs until the window is closed
	{
		ui.BeginFrame();

		processInput(window, camera_position, camera_zoom, camera_move_speed, &g_smooth_track_points);
		camera_position += camera_velocity;  
		camera_velocity *= friction;
		
		glClearColor(26.0f / 255.0f, 26.0f / 255.0f, 26.0f / 255.0f, 1.0f); // Set the clear color for the window (background color)
		glClear(GL_COLOR_BUFFER_BIT); // Clear the color buffer with the specified clear color


		// Aspect Ratio Calculation 
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

		// Creating orthographic projection matrix with camera
		glm::mat4 projection = glm::ortho(
			-zoomedHorizontal + camera_position.x,  // left
			zoomedHorizontal + camera_position.x,  // right
			-zoomedVertical + camera_position.y,    // bottom
			zoomedVertical + camera_position.y,    // top
			-1.0f, 1.0f
		);

		// Track creation and drawing
		glUseProgram(shader_program);

		GLint projLoc = glGetUniformLocation(shader_program, "projection");
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

		GLint colorLoc = glGetUniformLocation(shader_program, "uColor");

		glBindVertexArray(vao);

		std::vector<glm::vec2> borderLayer;
		std::vector<glm::vec2> asphaltLayer;

		size_t pointCount = 0;
		std::vector<glm::vec2> triangleStripPoints;
		{
			std::lock_guard<std::mutex> lock(points_mutex);
			pointCount = points.size();

			if (pointCount > 1)
			{
				g_is_map_loaded = true;

				// radius: 0.02f (rounding), segments: 10 (corner smoth)
				float CornerRadius = TrackConstants::TRACK_CORNER_RADIUS;

				// 1. Filter noise (points too close)
				std::vector<glm::vec2> filteredPoints = filterPointsByDistance(points, 0.05f);

				// 2. Simplify path (remove wavy lines on straights)
				std::vector<glm::vec2> simplifiedPoints = simplifyPath(filteredPoints, 0.02f);

				// 3. Generate rounded corners
				std::vector<SplinePoint> smoothPoints = interpolateRoundedPolyline(
					simplifiedPoints, 
					CornerRadius, 
					TrackConstants::TRACK_CORNER_SEGMENTS
				);
				
				// Store for vehicle simulation
				g_smooth_track_points = smoothPoints;

				borderLayer = generateTriangleStripFromLine(smoothPoints, TrackConstants::TRACK_BORDER_WIDTH);
				asphaltLayer = generateTriangleStripFromLine(smoothPoints, TrackConstants::TRACK_ASPHALT_WIDTH);


			}
		}

		if (borderLayer.size() > 0)
		{
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, borderLayer.size() * sizeof(glm::vec2), borderLayer.data(), GL_DYNAMIC_DRAW);

			glUniform3f(colorLoc, 1.0f, 1.0f, 1.0f); // White color for border
			glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)borderLayer.size());
		}

		if (asphaltLayer.size() > 0)
		{
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, asphaltLayer.size() * sizeof(glm::vec2), asphaltLayer.data(), GL_DYNAMIC_DRAW);

			glUniform3f(colorLoc, 0.3f, 0.3f, 0.3f); // Grey color for asphalt
			glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)asphaltLayer.size());
		}

		// M key removed - use T to create vehicles with automatic simulation



		// ========================== UI RENDERING ==========================

		ui.Render();
		ui.EndFrame();
		
		// ✅ Рисуем машины только если карта загружена
		if (g_is_map_loaded) { 
			renderAllVehicles(shader_program, vao, vbo, projection, camera_position, camera_zoom); 
		}
		// check and call events and swap the buffers
		 
		glfwPollEvents(); // Poll for and process events (e.g., keyboard, mouse)
		glfwSwapBuffers(window); // Swap the front and back buffers (render image display)
		
	}
	

	// ========================== CLEAN UP ==========================
	ui.Shutdown();

	serverStop();
	clientStop();
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteProgram(shader_program);
	glfwTerminate(); // Clean up all resources allocated by GLFW and exit

	return 0;

	// Uztaisit Start Stop sistemu lai pec N laiku MVP nepardota informacija, lai taupitu energiju

}
