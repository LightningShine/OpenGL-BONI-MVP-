#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Input.h"
#include "VENCHILEH.h"
#include "TelemetryServer.h"

// ==================== CONSTANTS ====================
namespace Config
{
	constexpr int WINDOW_WIDTH = 800;
	constexpr int WINDOW_HEIGHT = 800;
	constexpr const char* WINDOW_TITLE = "Race Map";

	constexpr float CAMERA_MOVE_SPEED = 0.001f;
	constexpr float CAMERA_FRICTION = 0.85f;
	constexpr float ZOOM_MIN = 0.1f;
	constexpr float ZOOM_MAX = 10.0f;
	constexpr float ZOOM_SENSITIVITY = 0.1f;

	constexpr float MAP_BOUND_X = 2.0f;
	constexpr float MAP_BOUND_Y = 2.0f;
	constexpr double MAP_RANGE = 1.0;

	constexpr float TRACK_BORDER_WIDTH = 0.04f;
	constexpr float TRACK_ASPHALT_WIDTH = 0.035f;
	constexpr float TRACK_CLOSE_THRESHOLD = 0.01f;
	constexpr int TRACK_INTERPOLATION_POINTS = 10;
}

// ==================== GLOBAL STATE ====================
VenchileManager venchileManager;
TelemetryServer telemetryServer;
std::atomic<bool> g_trackLoaded(false);

// ==================== SHADERS ====================
static const char* VERTEX_SHADER_SOURCE = R"(
    #version 460 core
    layout (location = 0) in vec2 aPos;
    uniform mat4 projection;
    void main()
    {
        gl_Position = projection * vec4(aPos.x, aPos.y, 0.0, 1.0); 
    }
)";

static const char* FRAGMENT_SHADER_SOURCE = R"(
    #version 460 core
    out vec4 FragColor;
    uniform vec3 uColor;
    void main()
    {
        FragColor = vec4(uColor, 1.0f); 
    }
)";

// ==================== CALLBACKS ====================
void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	float* zoom = static_cast<float*>(glfwGetWindowUserPointer(window));
	*zoom *= (1.0f + static_cast<float>(yoffset) * Config::ZOOM_SENSITIVITY);
	*zoom = glm::clamp(*zoom, Config::ZOOM_MIN, Config::ZOOM_MAX);
}

void OnTelemetryReceived(const ParsedTelemetry& telemetry)
{
	venchileManager.UpdateFromTelemetry(telemetry);
}

// ==================== INPUT PROCESSING ====================
void ProcessInput(GLFWwindow* window, glm::vec2& cameraPos, float zoom)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(window, true);
	}

	float moveAmount = Config::CAMERA_MOVE_SPEED / zoom;

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		cameraPos.y += moveAmount;

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		cameraPos.y -= moveAmount;

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		cameraPos.x -= moveAmount;

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		cameraPos.x += moveAmount;
}

// ==================== SHADER UTILITIES ====================
GLuint CompileShader(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	int success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char infoLog[512];
		glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
		std::cerr << "[Shader] Compilation failed: " << infoLog << std::endl;
	}
	return shader;
}

GLuint CreateShaderProgram(const char* vertexSource, const char* fragmentSource)
{
	GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
	GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return program;
}

// ==================== OPENGL SETUP ====================
bool InitOpenGL(GLFWwindow*& window)
{
	if (!glfwInit())
	{
		std::cerr << "[GLFW] Failed to initialize\n";
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	window = glfwCreateWindow(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, Config::WINDOW_TITLE, nullptr, nullptr);
	if (!window)
	{
		std::cerr << "[GLFW] Failed to create window\n";
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
	{
		std::cerr << "[GLAD] Failed to initialize\n";
		return false;
	}

	glViewport(0, 0, Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT);
	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

	return true;
}

void SetupBuffers(GLuint& VAO, GLuint& VBO)
{
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);
}

// ==================== TRACK RENDERING ====================
bool IsTrackClosed(const std::vector<glm::vec2>& points)
{
	if (points.size() < 3) return false;
	
	float distance = glm::length(points.back() - points.front());
	return distance < Config::TRACK_CLOSE_THRESHOLD;
}

void RenderTrackLayer(GLuint VBO, GLint colorLoc, const std::vector<glm::vec2>& layer, const glm::vec3& color)
{
	if (layer.empty()) return;

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, 
		static_cast<GLsizeiptr>(layer.size() * sizeof(glm::vec2)), 
		layer.data(), 
		GL_DYNAMIC_DRAW);
	
	glUniform3f(colorLoc, color.r, color.g, color.b);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(layer.size()));
}

// ==================== MAIN ====================
int main()
{
	std::cout << "========================================\n";
	std::cout << "   RaceMap - Starting Application\n";
	std::cout << "========================================\n";

	// Start telemetry server
	telemetryServer.SetCallback(OnTelemetryReceived);
	if (telemetryServer.Start(TELEMETRY_DEFAULT_PORT))
	{
		std::cout << "[Main] Telemetry server started on port " << TELEMETRY_DEFAULT_PORT << "\n";
	}
	else
	{
		std::cerr << "[Main] ERROR: Failed to start telemetry server!\n";
	}

	// Initialize OpenGL
	GLFWwindow* window = nullptr;
	if (!InitOpenGL(window))
	{
		return -1;
	}

	// Setup buffers and shaders
	GLuint VAO, VBO;
	SetupBuffers(VAO, VBO);
	GLuint shaderProgram = CreateShaderProgram(VERTEX_SHADER_SOURCE, FRAGMENT_SHADER_SOURCE);

	// Camera state
	glm::vec2 cameraPosition(0.0f);
	glm::vec2 cameraVelocity(0.0f);
	float cameraZoom = 1.0f;

	glfwSetWindowUserPointer(window, &cameraZoom);
	glfwSetScrollCallback(window, ScrollCallback);

	// Track data (shared with input thread)
	std::vector<glm::vec2> points;
	std::mutex pointsMutex;
	std::atomic<bool> running(true);

	std::thread inputThread(ChoseInputMode, std::ref(points), std::ref(pointsMutex), std::ref(running));

	// Render loop
	while (!glfwWindowShouldClose(window))
	{
		if (!running)
		{
			glfwSetWindowShouldClose(window, true);
		}

		ProcessInput(window, cameraPosition, cameraZoom);
		
		// Apply physics
		cameraPosition += cameraVelocity;
		cameraVelocity *= Config::CAMERA_FRICTION;
		cameraPosition = glm::clamp(cameraPosition, 
			glm::vec2(-Config::MAP_BOUND_X, -Config::MAP_BOUND_Y),
			glm::vec2(Config::MAP_BOUND_X, Config::MAP_BOUND_Y));

		// Clear screen
		glClearColor(0.0f, 0.0f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Calculate projection
		int windowWidth, windowHeight;
		glfwGetWindowSize(window, &windowWidth, &windowHeight);
		
		double aspectRatio = static_cast<double>(windowWidth) / static_cast<double>(windowHeight);
		double horizontalBound = (aspectRatio >= 1.0) ? Config::MAP_RANGE * aspectRatio : Config::MAP_RANGE;
		double verticalBound = (aspectRatio >= 1.0) ? Config::MAP_RANGE : Config::MAP_RANGE / aspectRatio;

		float zoomedH = static_cast<float>(horizontalBound) / cameraZoom;
		float zoomedV = static_cast<float>(verticalBound) / cameraZoom;

		glm::mat4 projection = glm::ortho(
			-zoomedH + cameraPosition.x, zoomedH + cameraPosition.x,
			-zoomedV + cameraPosition.y, zoomedV + cameraPosition.y,
			-1.0f, 1.0f
		);

		// Setup shader
		glUseProgram(shaderProgram);
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
		GLint colorLoc = glGetUniformLocation(shaderProgram, "uColor");

		glBindVertexArray(VAO);

		// Process track points
		std::vector<glm::vec2> borderLayer, asphaltLayer;
		bool trackClosed = false;
		{
			std::lock_guard<std::mutex> lock(pointsMutex);
			
			if (points.size() > 1)
			{
				auto smoothPoints = InterpolatePoints(points, Config::TRACK_INTERPOLATION_POINTS);
				borderLayer = GenerateTriangleStripFromLine(smoothPoints, Config::TRACK_BORDER_WIDTH);
				asphaltLayer = GenerateTriangleStripFromLine(smoothPoints, Config::TRACK_ASPHALT_WIDTH);
				trackClosed = IsTrackClosed(points);
			}
		}

		g_trackLoaded = !borderLayer.empty() && trackClosed;

		// Render track
		RenderTrackLayer(VBO, colorLoc, borderLayer, glm::vec3(1.0f));           // White border
		RenderTrackLayer(VBO, colorLoc, asphaltLayer, glm::vec3(0.3f));          // Gray asphalt

		// Render vehicles
		venchileManager.RenderVenchiles(g_trackLoaded);

		glfwPollEvents();
		glfwSwapBuffers(window);
	}

	// Cleanup
	running = false;
	telemetryServer.Stop();

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteProgram(shaderProgram);
	glfwTerminate();

	inputThread.join();

	return 0;
}