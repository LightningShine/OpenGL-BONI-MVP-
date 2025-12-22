#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Input.h"
<<<<<<< HEAD

using namespace std;

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
}

void processInput(GLFWwindow* window, glm::vec2& cameraPos, float& zoom, float speed)
{
	// Close when press ESC
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	// Camera movment (W/A/S/D or Arrows)
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		cameraPos.y += speed / zoom;  // Up

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		cameraPos.y -= speed / zoom;  // down

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		cameraPos.x -= speed / zoom;  // Left

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		cameraPos.x += speed / zoom;  // Right

	// Zoom limits
	if (zoom < 0.1f) zoom = 0.1f;   // Minimum
	if (zoom > 10.0f) zoom = 10.0f; // Maximum
}

// Callback for scroolling mouse wheel
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	float* zoom = (float*)glfwGetWindowUserPointer(window);
	*zoom *= (float)(1.0f + yoffset * 0.1f);  // Aspect of zoom change

	// Ограничение зума
	if (*zoom < 0.1f) *zoom = 0.1f;
	if (*zoom > 10.0f) *zoom = 10.0f;
}


//Shaders

const char* vertexShaderSource = R"(
=======
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
>>>>>>> c784474956008c989a0e5ee699405981940db8bd
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
<<<<<<< HEAD
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);  // Determine OpenGL major version OpenGL 4.X
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);	// Determine OpenGL minor version OpenGL X.6
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	// Use the core-profile
	// get access to a et access to a smaller subset 
	// of OpenGL features without backwards - compatible features we no longer need
=======
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
>>>>>>> c784474956008c989a0e5ee699405981940db8bd

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		cameraPos.y += moveAmount;

<<<<<<< HEAD

	GLFWwindow* window = glfwCreateWindow(Width, Height, "Race Map", NULL, NULL); // Create a windowed mode at windows 800x800, titled Race Map
	if (window == NULL) // Check if the window was created successfully
	{
		cout << "Failed to create GLFW window" << endl;
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window); // Make the window's context in current thread

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) // Initialize GLAD before calling any OpenGL function
	{
		cout << "Failed to initialize GLAD" << endl;
		return -1;
	}

	glViewport(0, 0, Width, Height); // Set the OpenGL viewport to cover the whole window

	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	// Register the callback function to adjust the viewport when the window is resized


	GLuint VAO, VBO, EBO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);


	// Vertex Buffer Object
	glBindVertexArray(VAO);
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glBindVertexArray(VAO);

	//glBindBuffer(GL_ARRAY_BUFFER, VBO);

	//Index Buffer Object
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

	//Vertex Attribute
	//glEnableVertexAttribArray(0);

	//glBindVertexArray(0);


	// ================= Shader compilation ===================
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);

	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);


	// Shader compile check
=======
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
>>>>>>> c784474956008c989a0e5ee699405981940db8bd

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

<<<<<<< HEAD
		std::vector<glm::vec2> borderLayer;
		std::vector<glm::vec2> asphaltLayer;

		size_t pointCount = 0;
		std::vector<glm::vec2> triangleStripPoints;
=======
		// Process track points
		std::vector<glm::vec2> borderLayer, asphaltLayer;
		bool trackClosed = false;
>>>>>>> c784474956008c989a0e5ee699405981940db8bd
		{
			std::lock_guard<std::mutex> lock(pointsMutex);
			
			if (points.size() > 1)
			{
<<<<<<< HEAD
				// Интерполируем точки для плавности
				std::vector<glm::vec2> smoothPoints = InterpolatePoints(points, 10);

				borderLayer = GenerateTriangleStripFromLine(smoothPoints, 0.04f);
				asphaltLayer = GenerateTriangleStripFromLine(smoothPoints, 0.035f);
			}
		}

		if (borderLayer.size() > 0)
		{
			glBindBuffer(GL_ARRAY_BUFFER, VBO);
			glBufferData(GL_ARRAY_BUFFER, borderLayer.size() * sizeof(glm::vec2), borderLayer.data(), GL_DYNAMIC_DRAW);

			glUniform3f(colorLoc, 1.0f, 1.0f, 1.0f); // White color for border
			glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)borderLayer.size());
		}

		if (asphaltLayer.size() > 0)
		{
			glBindBuffer(GL_ARRAY_BUFFER, VBO);
			glBufferData(GL_ARRAY_BUFFER, asphaltLayer.size() * sizeof(glm::vec2), asphaltLayer.data(), GL_DYNAMIC_DRAW);

			glUniform3f(colorLoc, 0.3f, 0.3f, 0.3f); // Grey color for asphalt
			glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)asphaltLayer.size());
		}

=======
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
>>>>>>> c784474956008c989a0e5ee699405981940db8bd

		glfwPollEvents();
		glfwSwapBuffers(window);
	}
<<<<<<< HEAD
	
	running = false; // Signal the input thread to stop
	
=======

	// Cleanup
	running = false;
	telemetryServer.Stop();
>>>>>>> c784474956008c989a0e5ee699405981940db8bd

	// ========================== CLEAN UP ==========================
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteProgram(shaderProgram);
	glfwTerminate();

	inputThread.join();



	return 0;
<<<<<<< HEAD

	// Uztaisit Start Stop sistemu lai pec N laiku MVP nepardoda informaciju, lai taupitu energiju

=======
>>>>>>> c784474956008c989a0e5ee699405981940db8bd
}