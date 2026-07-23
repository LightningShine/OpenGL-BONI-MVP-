// ============================================================================
// Author: Andrejs Deikuns
// Created: 2025-10-10
// ============================================================================

#pragma once
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include <string>
#include <regex>
#include <locale>
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include "../../libraries/include/stb_image.h"


// === WINDOWS BORDER COLOR ===
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>		// For glfwGetWin32Window
#include <dwmapi.h>					// DwmSetWindowAttribute function
#pragma comment(lib, "dwmapi.lib")  // Link with dwmapi.lib
#endif

// =================================
// === 3RD LIBRARIES ===
//#include <glm/glm.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// =================================
// === PROJECTS FILES ===
#include "../Config.h"
#include "../ui/UI_Config.h"
#include "../ui/ui_scale.hpp"
#include "DeviceRegistry.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../rendering/Render.h"          
#include "../rendering/VehicleNameRenderer.h"
#include "../../UI.h"
#include "../../UI_Elements.h"
#include "../network/Server.h"
#include "../network/TrackServerClient.h"
#include "../network/ESP32_Code.h"
#include "../network/SimulationServer.h"
#include "../track/TrackRecorder.h"
#include "../vehicle/Vehicle.h"
#include "../racing/RaceManager.h"
#include "../racing/ModeManager/ModeManager.h"


using namespace std;

UI* g_ui = nullptr;
ModeManager* g_mode_manager = nullptr;


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
std::mutex g_track_mutex;


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window, glm::vec2& camera_pos, float& zoom, float& rotation, float speed, 
const std::vector<SplinePoint>* smooth_track = nullptr, 
const std::vector<glm::vec2>* track_points = nullptr, std::mutex* points_mutex = nullptr)
{
	// Block OpenGL keyboard movement if user is typing in a UI input field
	if (g_ui && g_ui->WantsKeyboardCapture()) 
		return;

	// Close when press ESC
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

#if NETWORKING_ENABLED
	// Networking shortcuts are handled by UI so we can show connection/create modal.
#endif
	

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
			// Generate unique ID for simulation
         int vehicle_id = -1;
			{
				std::lock_guard<std::mutex> lock(g_vehicles_mutex);
				for (int id = 1; id <= 99; ++id)
				{
					if (g_vehicles.find(id) == g_vehicles.end())
					{
						vehicle_id = id;
						break;
					}
				}
			}
			if (vehicle_id == -1)
			{
				std::cout << "[SIM] Cannot start simulation - no free race IDs (1-99)" << std::endl;
			}
			else
			{

               std::cout << "[SIM] Starting vehicle #" << vehicle_id << " simulation on track" << std::endl;

			// ✅ DON'T create vehicle here! Let first telemetry packet create it.
			// This avoids coordinate mismatch between normalized→GPS→normalized roundtrip.
			// The simulation will send packets, and processIncomingTelemetry will create the vehicle.

              // Start simulation - first packet will CREATE the vehicle
				simulateVehicleMovement(vehicle_id, *smooth_track);
			}
		}
	}
	if (glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE)
	{
		wasTPressed = false;
	}

	// Y key: test track recording without hardware by generating a circle of telemetry packets.
	static bool wasYPressed = false;
	if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS && !wasYPressed)
	{
		wasYPressed = true;

		if (!g_is_map_loaded)
		{
			std::cout << "[TRACK-REC] Cannot test record: map origin not loaded (load a map/track first)." << std::endl;
		}
		else
		{
			constexpr int32_t kTestProtoId = 4242;
			constexpr int32_t kTestVehicleId = 1; // Recorder will be started for this race id; mapping uses proto id.

			TrackRecorder::Settings s;
			s.minPointDistanceNorm = 0.002f;
			s.closeRadiusNorm = 0.01f;
			s.minPointsToClose = 200;
			s.minLoopLengthMeters = 100.0f;
			s.pointsPerSegment = 6;
			TrackRecorder::Start(kTestVehicleId, s);
			std::cout << "[TRACK-REC] Test recording started (press Y again when done is not needed; auto closes)." << std::endl;

			// Generate telemetry: a circle around origin in UTM meters then convert to lat/lon.
			std::thread([=]() {
				using namespace GeographicLib;
				const bool northp = (g_map_origin.m_origin_zone_char >= 'N');
				const double baseE = g_map_origin.m_origin_meters_easting;
				const double baseN = g_map_origin.m_origin_meters_northing;
				const int zone = g_map_origin.m_origin_zone_int;

				TelemetryPacket packet{};
				packet.MagicMarker = PACKET_MAGIC_DATA;
				packet.ID = kTestProtoId;
				packet.fixtype = 4;
				packet.speed = 6000; // 60.00 km/h
				packet.acceleration = 0;
				packet.gForceX = 0;
				packet.gForceY = 0;

				const int steps = 1200;
				const double radiusMeters = 80.0;
				uint32_t t = 0;
				for (int i = 0; i < steps; ++i)
				{
					const double a = (static_cast<double>(i) / static_cast<double>(steps)) * (SimulationConstants::TWO_PI);
					const double e = baseE + std::cos(a) * radiusMeters;
					const double n = baseN + std::sin(a) * radiusMeters;
					double lat = 0.0;
					double lon = 0.0;
					UTMUPS::Reverse(zone, northp, e, n, lat, lon);
					packet.lat = static_cast<int32_t>(lat * 1e7);
					packet.lon = static_cast<int32_t>(lon * 1e7);
					packet.time = t;
					processIncomingTelemetry(packet);
					t += 16;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}

				// Try finalize and save.
				if (TrackRecorder::FinalizeNow())
				{
					std::cout << "[TRACK-REC] Finalized. Saving to track_recorded.bin" << std::endl;
					TrackRecorder::SaveToFile("track_recorded.bin", g_map_origin);
				}
				else
				{
					std::cout << "[TRACK-REC] Not finalized (did not close loop)." << std::endl;
				}
			}).detach();
		}
	}
	if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_RELEASE)
	{
		wasYPressed = false;
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
			rotation += CameraConstants::CAMERA_ROTATION_SPEED;
			if (rotation > CameraConstants::CAMERA_ROTATION_MAX)
				rotation = CameraConstants::CAMERA_ROTATION_MAX;
		}
		
		if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		{
			rotation -= CameraConstants::CAMERA_ROTATION_SPEED;
			if (rotation < CameraConstants::CAMERA_ROTATION_MIN)
				rotation = CameraConstants::CAMERA_ROTATION_MIN;
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
	
	// Ctrl+P (print results) and Ctrl+S (save results) are handled in the UI
	// layer (UI.cpp keyboard shortcuts) so they share BuildResultsText().

	static bool wasPPressed = false;
	static bool isWaitingForVehicleId = false;
	static double focusInputStartTime = 0.0;
	static const double FOCUS_INPUT_TIMEOUT = 10.0; // 10 seconds

	// P key: Toggle focus mode or reset focus
	if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !wasPPressed)
	{
		wasPPressed = true;

		if (isWaitingForVehicleId) {
			// Cancel input mode
			isWaitingForVehicleId = false;
			std::cout << "[FOCUS] Vehicle selection cancelled" << std::endl;
		}
		else if (g_focused_vehicle_id != -1) {
			// Reset to leader
			g_focused_vehicle_id = -1;
			std::cout << "[FOCUS] Reset to leader tracking" << std::endl;
		}
		else {
			// Start input mode
			isWaitingForVehicleId = true;
			focusInputStartTime = glfwGetTime();
			std::cout << "[FOCUS] Enter vehicle ID (1-9) within 10 seconds..." << std::endl;
		}
	}
	if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE)
	{
		wasPPressed = false;
	}

	// Handle vehicle ID input (1-9)
	if (isWaitingForVehicleId)
	{
		double currentTime = glfwGetTime();
		if (currentTime - focusInputStartTime > FOCUS_INPUT_TIMEOUT) {
			isWaitingForVehicleId = false;
			std::cout << "[FOCUS] Input timeout - cancelled" << std::endl;
		}
		else {
			// Check for number keys 1-9
			for (int key = GLFW_KEY_1; key <= GLFW_KEY_9; key++) {
				if (glfwGetKey(window, key) == GLFW_PRESS) {
					int vehicleId = key - GLFW_KEY_0;

					// Check if vehicle exists
					std::lock_guard<std::mutex> lock(g_vehicles_mutex);
					if (g_vehicles.find(vehicleId) != g_vehicles.end()) {
						g_focused_vehicle_id = vehicleId;
						isWaitingForVehicleId = false;
						std::cout << "[FOCUS] Now tracking Vehicle #" << vehicleId << std::endl;
					}
					else {
						isWaitingForVehicleId = false;
						std::cout << "[FOCUS] Vehicle #" << vehicleId << " does not exist" << std::endl;
					}
					break;
				}
			}
		}
	}
}

// Callback for scroolling mouse wheel
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	// Block OpenGL zoom if ImGui is holding the mouse (e.g., scrolling a modal or dropdown)
	if (g_ui && g_ui->WantsMouseCapture()) 
		return;

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
	if (context && context->ui)
	{
		for (int i = 0; i < count; i++)
			context->ui->HandleDroppedFile(paths[i]);
	}
}


//Shaders

const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aTexCoord;  // UV coordinates for checkered pattern
    
    uniform mat4 projection;
    
    out vec2 vTexCoord;  // Pass to fragment shader
    
    void main()
    {
        gl_Position = projection * vec4(aPos.x, aPos.y, 0.0, 1.0);
        vTexCoord = aTexCoord;  // Forward UVs
    }
)";

const char* fragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    
    uniform vec3 uColor;
    uniform float uAlpha;
    uniform bool uCheckered;  // Enable procedural checkered pattern
    
    in vec2 vTexCoord;  // UV coordinates from vertex shader
    
    void main()
    {
        if (uCheckered) {
            // Procedural checkered pattern (4 squares wide, 1 tall)
            vec2 uv_scaled = vTexCoord * vec2(4.0, 1.0);
            float check = mod(floor(uv_scaled.x) + floor(uv_scaled.y), 2.0);
            FragColor = vec4(vec3(check), 1.0);  // Black (0) and White (1)
        } else {
            FragColor = vec4(uColor, uAlpha);
        }
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
#ifdef _WIN32
	// Console in UTF-8, otherwise Cyrillic (track/driver names, paths) prints as '?'
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	// Explicit TrueType font: UTF-8 needs one, and conhost resets to a tiny
	// raster font when the codepage changes
	{
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_FONT_INFOEX cf{};
		cf.cbSize = sizeof(cf);
		if (GetCurrentConsoleFontEx(hOut, FALSE, &cf))
		{
			wcscpy_s(cf.FaceName, L"Consolas");
			cf.dwFontSize.X = 0;
			cf.dwFontSize.Y = 16;
			SetCurrentConsoleFontEx(hOut, FALSE, &cf);
		}
	}
#endif

	// Стартовый баннер. Сетевой стек — WebSocket-клиент Track Server, он
	// работает на всех архитектурах (старый GNS удалён, см. Server.h).
	std::cout << "==========================================" << std::endl;
	std::cout << "   " << UIConfig::APP_NAME << " Telemetry System" << std::endl;
	std::cout << "==========================================" << std::endl;
#if defined(_M_ARM64)
	std::cout << "[PLATFORM] Windows ARM64" << std::endl;
#elif defined(_M_X64)
	std::cout << "[PLATFORM] Windows x64" << std::endl;
#else
	std::cout << "[PLATFORM] Windows (unknown arch)" << std::endl;
#endif
	std::cout << "[FEATURES] Track Server client (WebSocket), COM telemetry, "
	             "simulation, OpenGL rendering" << std::endl;
	std::cout << "==========================================" << std::endl;
	std::cout << std::endl;


	std::cout << "[MAIN] Starting application..." << std::endl;

#ifdef _WIN32
	// === OpenGL renderer fallback (Mesa3D) ===
	// On machines without a GL 3.3 driver (clean VMs, RDP, very old GPUs) the
	// native opengl32 can't create a 3.3 context. We try the native driver first;
	// if it fails the window==NULL branch below relaunches us with BONI_USE_MESA=1,
	// which preloads the bundled Mesa software/D3D12 renderer here BEFORE GLFW
	// touches opengl32 (so opengl32.dll resolves to Mesa's copy by base name).
	if (GetEnvironmentVariableA("BONI_USE_MESA", nullptr, 0) > 0)
	{
		wchar_t dir[MAX_PATH];
		GetModuleFileNameW(NULL, dir, MAX_PATH);
		if (wchar_t* slash = wcsrchr(dir, L'\\')) *slash = L'\0';  // dir = exe folder
		wchar_t mesaDir[MAX_PATH], mesaDll[MAX_PATH];
		swprintf(mesaDir, MAX_PATH, L"%s\\mesa", dir);
		swprintf(mesaDll, MAX_PATH, L"%s\\opengl32.dll", mesaDir);
		SetDllDirectoryW(mesaDir);  // let Mesa resolve its own deps (libgallium_wgl, dxil)
		if (LoadLibraryExW(mesaDll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH))
			std::cout << "[MAIN] Using bundled Mesa OpenGL renderer" << std::endl;
		else
			std::cerr << "[MAIN] Warning: failed to load Mesa opengl32.dll (err "
			          << GetLastError() << ")" << std::endl;
	}
#endif

	glfwInit();
	std::cout << "[MAIN] GLFW initialized" << std::endl;
	
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);  // OpenGL 3.3 — widest hardware + Mesa software support
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);	// OpenGL 3.3
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
	// DPI: GLFW_SCALE_TO_MONITOR НЕ включаем — GLFW ресайзил бы окно только по
	// системному DPI, а наш масштаб включает ещё и физическую плотность
	// монитора. Окно пересчитывает UI::apply_ui_scale_change() сам, одним
	// коэффициентом с контентом (см. src/ui/ui_scale.hpp).

	std::cout << "[MAIN] Creating window " << Width << "x" << Height << "..." << std::endl;
	GLFWwindow* window = glfwCreateWindow(Width, Height, UIConfig::APP_NAME, NULL, NULL);
	
	std::cout << "[MAIN] Window created, enabling multisampling..." << std::endl;
	
	if (window == NULL)
	{
		cout << "[ERROR] Failed to create GLFW window" << endl;
#ifdef _WIN32
		// Native OpenGL couldn't give us a 3.3 context. Relaunch ONCE using the
		// bundled Mesa renderer (BONI_USE_MESA preloads it before GLFW starts).
		if (GetEnvironmentVariableA("BONI_USE_MESA", nullptr, 0) == 0)
		{
			std::cout << "[MAIN] Native OpenGL unavailable, retrying with Mesa..." << std::endl;
			glfwTerminate();
			SetEnvironmentVariableW(L"BONI_USE_MESA", L"1");
			wchar_t exePath[MAX_PATH];
			GetModuleFileNameW(NULL, exePath, MAX_PATH);
			STARTUPINFOW si{}; si.cb = sizeof(si);
			PROCESS_INFORMATION pi{};
			if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
			{
				WaitForSingleObject(pi.hProcess, INFINITE);
				DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
				CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
				return (int)code;
			}
			std::cerr << "[MAIN] Failed to relaunch with Mesa" << std::endl;
		}
#endif
		glfwTerminate();
		return -1;
	}
	
	std::cout << "[MAIN] Window created successfully" << std::endl;
	ui_scale::init(window);



	// === SET WINDOW ICON ===
	{
		int icon_w = 0, icon_h = 0, icon_channels = 0;
		unsigned char* icon_pixels = stbi_load("styles/images/Icon", &icon_w, &icon_h, &icon_channels, 4);
		if (!icon_pixels)
			icon_pixels = stbi_load("./styles/icons/PNG/Icon.png", &icon_w, &icon_h, &icon_channels, 4);
		if (icon_pixels)
		{
			GLFWimage icon_image;
			icon_image.width  = icon_w;
			icon_image.height = icon_h;
			icon_image.pixels = icon_pixels;
			glfwSetWindowIcon(window, 1, &icon_image);
			stbi_image_free(icon_pixels);
			std::cout << "[MAIN] Window icon set from styles/images/Icon" << std::endl;
		}
		else
		{
			std::cerr << "[MAIN] Warning: Could not load window icon from styles/images/Icon(.png)" << std::endl;
		}
	}

	// === CHANGE WINDOW BORDER COLOR (Windows 11) ===
#ifdef _WIN32
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
#endif


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

	if (const GLubyte* r = glGetString(GL_RENDERER))
		std::cout << "[MAIN] GL_RENDERER: " << (const char*)r << std::endl;
	if (const GLubyte* v = glGetString(GL_VERSION))
		std::cout << "[MAIN] GL_VERSION:  " << (const char*)v << std::endl;

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

	// ========================== Process Input & Callbacks ==========================
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

	UI ui;

	// Initialize Mode Manager
	ModeManager modeManager;
	g_mode_manager = &modeManager;
	modeManager.SetMode(RaceMode::CircuitRace);
	modeManager.SetPhase(RacePhase::Practice);

	AppContext appContext;
	appContext.zoom = &camera_zoom;
	appContext.points = &points;
	appContext.points_mutex = &points_mutex;
	appContext.ui = &ui;

	glfwSetWindowUserPointer(window, &appContext);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetDropCallback(window, drop_callback);

	// ================= UI Initialization ==================

	std::cout << "[MAIN] Initializing UI..." << std::endl;

	if (!ui.Initialize(window))
	{
		std::cerr << "Failed UI Initialization" << endl;
		glfwTerminate();
		return -1;
	}
	g_ui = &ui;

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

	// Variables moved above UI initialization

	// ========================== VEHICLE NAME RENDERER INITIALIZATION ==========================
	VehicleNameRenderer::Initialize();

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
	
	// ========================== DEVICE REGISTRY (имена/группы трекеров) ==========================
	// Локальная SQLite-база рядом с exe: помнит ID устройств и заданные имена.
	DeviceRegistry::instance().open("devices.db");

	// ========================== RACE MANAGER INITIALIZATION ==========================
	g_race_manager = new RaceManager();
	std::cout << "[MAIN] Race Manager initialized" << std::endl;




























































	// ========================== GRID VAO/VBO INITIALIZATION ==========================
	// Create grid VAO/VBO ONCE before render loop (NOT every frame!)
	GLuint grid_vao, grid_vbo;
	glGenVertexArrays(1, &grid_vao);
	glGenBuffers(1, &grid_vbo);
	std::cout << "Grid VAO/VBO created (VAO: " << grid_vao << ", VBO: " << grid_vbo << ")" << std::endl;

	// ========================== RENDER LOOP ==========================

	// Delta time calculation for physics-accurate timing
	auto lastFrameTime = std::chrono::steady_clock::now();

	while (!glfwWindowShouldClose(window)) // Main loop that runs until the window is closed
	{
		// ✅ CRITICAL: Skip rendering when window is minimized/iconified
		// Prevents OpenGL errors and crashes when context is unavailable
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
		{
			glfwWaitEvents();  // Sleep until window is restored (saves CPU)
			continue;
		}

		// ✅ Track received from the RAJAGP Track Server (socket thread) —
		// upload to the GPU here, on the thread that owns the GL context.
		{
			std::vector<glm::vec2> ts_left, ts_right;
			if (TrackServerClient::consumePendingTrack(ts_left, ts_right))
				TrackRenderer::rebuildTrackCacheFromEdges(ts_left, ts_right);
		}

		// ✅ Build track rendering cache if track was loaded from network
		// This must be done in main thread because OpenGL context is not thread-safe
		if (g_is_map_loaded && !TrackRenderer::isTrackCacheValid() && !g_smooth_track_points.empty())
		{
			std::cout << "[MAIN] Building track rendering cache in main thread..." << std::endl;

			// Copy spline points under mutex (network thread can update them during track load)
			std::vector<SplinePoint> track_copy;
			{
				std::lock_guard<std::mutex> track_lock(g_track_mutex);
				track_copy = g_smooth_track_points;
			}

			// Build cache directly from received spline points to avoid client-side reprocessing
			TrackRenderer::rebuildTrackCacheFromSplinePoints(track_copy);

			std::cout << "[MAIN] ✓ Track rendering cache built - track should now be visible!" << std::endl;
		}

		// Calculate delta time
		auto currentFrameTime = std::chrono::steady_clock::now();
		float deltaTime = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
		lastFrameTime = currentFrameTime;

		// Update Race Manager (lap timing logic)
		if (g_race_manager)
		{
			g_race_manager->Update(deltaTime);
		}
		
		ui.BeginFrame();

		processInput(window, camera_position, camera_zoom, camera_rotation, camera_move_speed, 
					&g_smooth_track_points, &points, &points_mutex);  // ✅ Pass track data for networking
		camera_position += camera_velocity;  
		camera_velocity *= friction;
		
		if (ui.IsProMode())
			glClearColor(13.0f / 255.0f, 13.0f / 255.0f, 13.0f / 255.0f, 1.0f); // PRO: dark canvas
		else
			glClearColor(26.0f / 255.0f, 26.0f / 255.0f, 26.0f / 255.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);


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
		glm::mat4 view_world = glm::mat4(1.0f);

		// 1. Move to screen center (camera position)
		view_world = glm::translate(view_world, glm::vec3(-camera_position.x, -camera_position.y, 0.0f));

		// 2. Rotate around screen center (camera position)
		view_world = glm::rotate(view_world, glm::radians(camera_rotation), glm::vec3(0.0f, 0.0f, 1.0f));

		glm::mat4 view_grid = glm::mat4(1.0f);
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
		
	if (!ui.IsProMode()) {
		// Grid and track only visible in standard (light) view
		renderGrid(shader_program, grid_vao, grid_vbo, camera_position, camera_zoom, camera_rotation, Width, Height,
		           (float)horizontalBound, (float)verticalBound);

		glUseProgram(shader_program);
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(viewProjection_world));
		TrackRenderer::renderCachedTrack(shader_program);
		TrackRenderer::renderStartFinishLine(shader_program, viewProjection_world);
		glUseProgram(shader_program);
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(viewProjection_world));
		TrackRenderer::renderStartFinishGrayLine(shader_program);
	}







	if (g_is_map_loaded && !ui.IsProMode()) {
		renderAllVehicles(shader_program, vao, vbo, viewProjection_world, camera_position, camera_zoom);
	}


	// ========================== UI RENDERING ==========================

		// Render UI AFTER track and vehicles so it's on top
		ui.Render();

		// Render race status bar
		if (g_mode_manager)
		{
			ui.RenderRaceStatusBar(g_mode_manager);
		}

		// Render compass and laptimer — hidden when PRO view is active
		if (ui.ShouldCloseSplash() && ui.getElements() && !ui.IsProMode())
		{
			ui.getElements()->drawCompass(camera_rotation, g_map_origin);

			if (g_race_manager)
			{
				int trackedVehicleId = g_focused_vehicle_id;
				if (trackedVehicleId == -1)
				{
					auto standings = g_race_manager->GetStandings();
					if (!standings.empty())
						trackedVehicleId = standings[0].vehicleID;
				}

				if (trackedVehicleId != -1)
				{
					float currentLapTime  = g_race_manager->GetVehicleCurrentLapTime(trackedVehicleId);
					float previousLapTime = g_race_manager->GetVehiclePreviousLapTime(trackedVehicleId);
					float bestLapTime     = g_race_manager->GetVehicleBestLapTime(trackedVehicleId);
					float deltaTime       = g_race_manager->GetVehicleLapDelta(trackedVehicleId);
					ui.getElements()->drawLapTimer(currentLapTime, previousLapTime, bestLapTime, deltaTime);
				}
				else
				{
					ui.getElements()->drawLapTimer(0.0f, -1.0f, -1.0f, 0.0f);
				}

				ui.getElements()->drawLeaderboard();
			}
		}
		
		ui.EndFrame();
		
		// check and call events and swap the buffers
		 
		glfwPollEvents(); // Poll for and process events (e.g., keyboard, mouse)
		glfwSwapBuffers(window); // Swap the front and back buffers (render image display)
		
	}
	

	// ========================== CLEAN UP ==========================
	
	// Clean up Race Manager
	if (g_race_manager)
	{
		delete g_race_manager;
		g_race_manager = nullptr;
		std::cout << "[MAIN] Race Manager destroyed" << std::endl;
	}
	
	ui.Shutdown();

	// Stop serial capture + COM port discovery thread on app shutdown
	stopRealDataCapture();
	stopComPortAutoDiscovery();

	TrackServerClient::stop();
	
	TrackRenderer::clearTrackCache();  // Clear track VAO/VBO
	VehicleNameRenderer::Shutdown();
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &grid_vao);  // Clean grid VAO/VBO
	glDeleteBuffers(1, &grid_vbo);
	glDeleteProgram(shader_program);
	glfwTerminate(); // Clean up all resources allocated by GLFW and exit

	return 0;

	// Uztaisit Start Stop sistemu lai pec N laiku MVP nepardota informacija, lai taupitu energiju

}
