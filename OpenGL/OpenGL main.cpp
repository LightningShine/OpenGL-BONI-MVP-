#pragma once
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Input.h"
#include "VENCHILEH.h"
#include "TelemetryServer.h"

// Global managers
VenchileManager venchileManager;
TelemetryServer telemetryServer;

// Track state - shared between threads
std::atomic<bool> g_trackLoaded(false);

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
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		cameraPos.y += speed / zoom;

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		cameraPos.y -= speed / zoom;

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		cameraPos.x -= speed / zoom;

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		cameraPos.x += speed / zoom;

	if (zoom < 0.1f) zoom = 0.1f;
	if (zoom > 10.0f) zoom = 10.0f;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	float* zoom = (float*)glfwGetWindowUserPointer(window);
	*zoom *= (float)(1.0f + yoffset * 0.1f);

	if (*zoom < 0.1f) *zoom = 0.1f;
	if (*zoom > 10.0f) *zoom = 10.0f;
}

// Telemetry callback - called from server thread
// Машины добавляются ВСЕГДА, но отрисовываются только когда трек загружен
void OnTelemetryReceived(const ParsedTelemetry& telemetry)
{
	venchileManager.UpdateFromTelemetry(telemetry);
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
	// ========================== TELEMETRY SERVER ==========================
	// Сервер запускается в ОТДЕЛЬНОМ ПОТОКЕ внутри TelemetryServer::Start()
	cout << "========================================\n";
	cout << "   RaceMap - Starting Application\n";
	cout << "========================================\n";

	telemetryServer.SetCallback(OnTelemetryReceived);
	
	if (telemetryServer.Start(TELEMETRY_DEFAULT_PORT))
	{
		cout << "[Main] Telemetry server thread started on port " << TELEMETRY_DEFAULT_PORT << "\n";
	}
	else
	{
		cerr << "[Main] ERROR: Failed to start telemetry server!\n";
	}

	// ========================== GLFW INIT ==========================
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	int Width = 800;
	int Height = 800;

	GLFWwindow* window = glfwCreateWindow(Width, Height, "Race Map", NULL, NULL);
	if (window == NULL)
	{
		cout << "Failed to create GLFW window" << endl;
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		cout << "Failed to initialize GLAD" << endl;
		return -1;
	}

	glViewport(0, 0, Width, Height);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// ============================ CUBE ============================
	float vertices[] = {
		0.5f,  0.5f,  // top right 0
		0.7f,  0.0f,  // middle right 1
		0.5f, -0.5f,  // bottom right 2
	   -0.5f, -0.5f,  // bottom left 3
	   -0.7f,  0.0f,  // middle left 4
	   -0.5f,  0.5f   // top left 5
	};

	unsigned int indices[] = { 0, 2, 1,
							   5, 3, 4,
							   5, 0, 2,
							   5, 3, 2 }; // How does it work?


	GLuint VAO, VBO, EBO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);


	// Vertex Buffer Object
	glBindVertexArray(VAO);
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	//Index Buffer Object
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	//Vertex Attribute
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);


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

	GLuint shaderProgram = glCreateProgram();

	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);

	glLinkProgram(shaderProgram);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	glEnable(GL_PROGRAM_POINT_SIZE);

	// ========================== Process Input ==========================
	glm::vec2 cameraPosition(0.0f, 0.0f);  // Camera Position (X, Y)
	float cameraZoom = 1.0f;                // Zoom (1.0 = Normal, >1 = Zoom)
	float cameraMoveSpeed = 0.001f;          // Camera movment speed
	//float cameraZoomSpeed = 0.1f;           // Zoom speed
	glm::vec2 cameraVelocity(0.0f, 0.0f);  // Velocity
	float friction = 0.85f;                 // Friction
	float mapBoundX = 2.0f;  // Map border X
	float mapBoundY = 2.0f;  // Map border Y

	glfwSetWindowUserPointer(window, &cameraZoom);
	glfwSetScrollCallback(window, scroll_callback);

	// Callback для скролла мыши



	// ========================== INPUT THREAD ==========================

	std::vector<glm::vec2> points;
	std::mutex  pointsMutex;
	std::atomic<bool> running(true);
	std::thread inputThread(
		ChoseInputMode,
	std::ref(points),
	std::ref(pointsMutex),
	std::ref(running)
	);


	int windowswidth;
	int windowsheight;

	double mapRange = 1.0; // Assuming the map range is normalized to 1.0

	double horizontalBound = mapRange;
	double verticalBound = mapRange;


	// ========================== RENDER LOOP ==========================

	while (!glfwWindowShouldClose(window)) // Main loop that runs until the window is closed
	{
		processInput(window, cameraPosition, cameraZoom, cameraMoveSpeed); // Process user input with camera
		cameraPosition += cameraVelocity;  // Применяем скорость
		cameraVelocity *= friction;
		
		if (!running) glfwSetWindowShouldClose(window, true); // If console closed, close the windows too

		glClearColor(0.0f, 0.0f, 0.1f, 1.0f); // Set the clear color for the window (background color)
		glClear(GL_COLOR_BUFFER_BIT); // Clear the color buffer with the specified clear color
		
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

		// Apply zoom (devide border on zoom)
		float zoomedHorizontal = (float)horizontalBound / (float)cameraZoom;
		float zoomedVertical = (float)verticalBound / (float)cameraZoom;

		cameraPosition.x = glm::clamp(cameraPosition.x, -mapBoundX, mapBoundX);
		cameraPosition.y = glm::clamp(cameraPosition.y, -mapBoundY, mapBoundY);

		// Creating orthographic projection matrix with camera
		glm::mat4 projection = glm::ortho(
			-zoomedHorizontal + cameraPosition.x,  // left
			zoomedHorizontal + cameraPosition.x,  // right
			-zoomedVertical + cameraPosition.y,    // bottom
			zoomedVertical + cameraPosition.y,    // top
			-1.0f, 1.0f
		);
		
		glUseProgram(shaderProgram);
		
		GLint projLoc = glGetUniformLocation(shaderProgram, "projection");
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
		
		GLint colorLoc = glGetUniformLocation(shaderProgram, "uColor");

		glBindVertexArray(VAO);

		std::vector<glm::vec2> borderLayer;
		std::vector<glm::vec2> asphaltLayer;

		size_t pointCount = 0;
		bool trackClosed = false;
		{
			std::lock_guard<std::mutex> lock(pointsMutex);
			pointCount = points.size();
			
			if (pointCount > 1)
			{
				// Интерполируем точки для плавности
				std::vector<glm::vec2> smoothPoints = InterpolatePoints(points, 10);

				borderLayer = GenerateTriangleStripFromLine(smoothPoints, 0.04f);
				asphaltLayer = GenerateTriangleStripFromLine(smoothPoints, 0.035f);
				
				// Check if track is closed (first point == last point)
				if (pointCount > 2)
				{
					glm::vec2 first = points.front();
					glm::vec2 last = points.back();
					float distance = glm::length(last - first);
					trackClosed = (distance < 0.01f); // threshold for "closed"
				}
			}
		}

		// Update track loaded state
		g_trackLoaded = (pointCount > 1) && trackClosed;

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

		// Render vehicles only if track is loaded and closed
		venchileManager.RenderVenchiles(g_trackLoaded);

		// check and call events and swap the buffers
		glfwPollEvents(); // Poll for and process events (e.g., keyboard, mouse)
		glfwSwapBuffers(window); // Swap the front and back buffers (render image display)
		
	}
	
	// ========================== CLEAN UP ==========================
	running = false;
	telemetryServer.Stop();

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteProgram(shaderProgram);
	glfwTerminate(); // Clean up all resources allocated by GLFW and exit

	inputThread.join(); // Wait for the input thread to finish

	return 0;

	// Uztaisit Start Stop sistēmu lai pēc N laiku MVP nepārdod informāciju, lai taupītu enerģiju

}