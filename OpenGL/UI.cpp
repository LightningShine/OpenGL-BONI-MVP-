#include "UI.h"
#include "libraries/include/imgui/imgui.h"
#include "libraries/include/imgui/backends/imgui_impl_glfw.h"
#include "libraries/include/imgui/backends/imgui_impl_opengl3.h"
#include <iostream>

UI::UI() : m_window(nullptr), m_context(nullptr)
{
}

UI::~UI()
{
	Shutdown();
}

bool UI::Initialize(GLFWwindow* window)
{
	if (!window)
	{
		std::cerr << "UI Initialization failed: GLFWwindow is null." << std::endl;
		return false;
	}
	m_window = window;

	// Creating imGUI context
	IMGUI_CHECKVERSION();
	m_context = ImGui::CreateContext();

	if(!m_context)
	{
		std::cerr << "UI Initialization failed: ImGui context creation failed." << std::endl;
		return false;
	}

	// Setup ImGui bindings
	ImGuiIO& io = ImGui::GetIO(); //io = input/output structure that we can use to configure ImGui

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

	// Conecting to GLFW and OpenGL
	if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
	{
		std::cerr << "UI Initialization failed: ImGui GLFW initialization failed." << std::endl;
		return false;
	}

	if (!ImGui_ImplOpenGL3_Init("#version 460"))
	{
		std::cerr << "UI Initialization failed: ImGui OpenGL3 initialization failed." << std::endl;
		return false;
	}

	std::cout << "UI Initialization succeeded." << std::endl;
	return true;

}

void UI::Shutdown()
{
	if (m_context)
	{
		// Shutdown ImGui bindings
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext(m_context);

		m_context = nullptr;
	}
}

void UI::BeginFrame()
{
	// Starting the new frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void UI::Render()
{
	// There we will call all our windows rendering functions (UI elemets)
	RenderTestWindow();
}	
void UI::EndFrame()
{
	// Rendering
	ImGui::Render(); // Compile UI to commands for drawing
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); // Do the drawing commands
}

void UI::RenderTestWindow()
{
	// ========== FIRST TEST WINDOW ==========

	ImGui::Begin("Hello Window");                          // Create a window called "Hello Window" and append into it.
	ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)

	static int counter = 0;
	if (ImGui::Button("Button"))							// Buttons return true when clicked (most widgets return true when edited/activated)
	{
		counter++;
		std::cout << "Button clicked " << counter << " times." << std::endl;
	}

	ImGui::SameLine(); // Simple line on this windows
	ImGui::Text("Counter = %d", counter);

	ImGui::End();
}