#pragma once
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

struct ImGuiContext;

class UI
{
	public:
		UI();
		~UI();

		// Life methods
		bool Initialize(GLFWwindow* window);
		void Shutdown();
		
		void BeginFrame();
		void Render();
		void EndFrame();

	private:
		GLFWwindow* m_window;
		ImGuiContext* m_context;

		// First windows
		void RenderTestWindow();

};