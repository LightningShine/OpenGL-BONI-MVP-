#pragma once
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
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

//Shaders

const char* vertexShaderSource = R"(
    #version 460 core
    layout (location = 0) in vec2 aPos; 
    void main()
    {
        gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0); 
    }
)";

const char* fragmentShaderSource = R"(
    #version 460 core
    out vec4 FragColor;
    void main()
    {
        FragColor = vec4(1.0f, 0.0f, 0.0f, 1.0f); 
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
	

	GLFWwindow* window = glfwCreateWindow(800, 600, "Race Map", NULL, NULL); // Create a windowed mode at windows 800x600, titled Race Map
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

	glViewport(0, 0, 800, 600); // Set the OpenGL viewport to cover the whole window

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
							   5, 3, 2}; // How does it work?


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

	GLuint fragmentShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);


	// Shader compile check

	int success;
	char infoLog[512];

	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
	if(!success)
	{
		glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
		cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << endl;
	}

	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
		cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << endl;
	}

	// =================== Create Shader Program ===================

	GLuint shaderProgram = glCreateProgram();

	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);

	glLinkProgram(shaderProgram);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	glEnable(GL_PROGRAM_POINT_SIZE);


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

	//string cordinate;
	//std::thread TestThread(
	//	InputOrigin,
	//	std::ref(cordinate)
	//);



	// ========================== RENDER LOOP ==========================

	while (!glfwWindowShouldClose(window)) // Main loop that runs until the window is closed
	{
		processInput(window); // Process user input (e.g., keyboard events)
		
		if (!running) glfwSetWindowShouldClose(window, true); // If console closed, close the windows too

		glClearColor(0.0f, 0.0f, 0.1f, 1.0f); // Set the clear color for the window (background color)
		glClear(GL_COLOR_BUFFER_BIT); // Clear the color buffer with the specified clear color
		
		glUseProgram(shaderProgram); // Use the shader program (0 means no shader program, using fixed-function pipeline)
		glBindVertexArray(VAO);

		size_t pointCount = 0;
		{
			std::lock_guard<std::mutex> lock(pointsMutex);
			pointCount = points.size();
			glBindBuffer(GL_ARRAY_BUFFER, VBO); // Updating VBO with new points   VBO update the point data
			glBufferData(GL_ARRAY_BUFFER, pointCount * sizeof(glm::vec2), points.data(), GL_DYNAMIC_DRAW);
			// Copy the new point data to the GPU from points.data()
		}

		if (pointCount > 0)
		{
			glLineWidth(5.0f);
			glPointSize(5.0f);
			glDrawArrays(GL_POINTS, 0, (GLsizei)pointCount); // Draw the points as GL_POINTS
			glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)pointCount); // Draw the points as GL_LINE_STRIP

		}

			//glLineWidth(5.0f);
			//glPointSize(5.0f);
		//glDrawArrays(GL_LINE_LOOP, 0, 6);
		
		//glDrawElements(GL_TRIANGLES, 12, GL_UNSIGNED_INT, 0);

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