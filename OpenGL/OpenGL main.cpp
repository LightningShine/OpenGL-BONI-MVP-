#pragma once
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Input.h"

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

	int Width = 800;
	int Height = 800;


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
		std::vector<glm::vec2> triangleStripPoints;
		{
			std::lock_guard<std::mutex> lock(pointsMutex);
			pointCount = points.size();
			
			if (pointCount > 1)
			{
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


		// check and call events and swap the buffers
		glfwPollEvents(); // Poll for and process events (e.g., keyboard, mouse)
		glfwSwapBuffers(window); // Swap the front and back buffers (render image display)
		
	}
	
	running = false; // Signal the input thread to stop
	

	// ========================== CLEAN UP ==========================
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteProgram(shaderProgram);
	glfwTerminate(); // Clean up all resources allocated by GLFW and exit

	inputThread.join(); // Wait for the input thread to finish



	return 0;

	// Uztaisit Start Stop sistemu lai pec N laiku MVP nepardoda informaciju, lai taupitu energiju

}