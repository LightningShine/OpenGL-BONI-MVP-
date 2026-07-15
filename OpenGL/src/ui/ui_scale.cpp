#include "ui_scale.hpp"

#include "UI_Config.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

namespace
{
	// Пересчёт по времени, а не по кадрам: одинаковая отзывчивость и на
	// 30 FPS, и на 240 FPS. Смена монитора/разрешения — редкое событие,
	// секунды за глаза, а enum мониторов не бесплатный.
	constexpr auto POLL_INTERVAL = std::chrono::seconds(1);

	// Настройка "UI Scale" из меню View переживает перезапуск.
	constexpr const char* USER_SCALE_FILE = "ui_scale.ini";

	GLFWwindow* g_window     = nullptr;
	float g_os_scale         = 1.0f;  // от ОС: настройка масштаба Windows
	float g_physical_scale   = 0.0f;  // от железа: PPI монитора / REFERENCE_PPI (0 = неизвестен)
	float g_user_scale       = 1.0f;  // множитель пользователя поверх всего
	float g_effective_scale  = 1.0f;  // итог, под который собраны шрифты
	bool  g_scale_changed    = false; // взводится при смене итога, гасится consume_*()
	std::chrono::steady_clock::time_point g_next_poll_time{};

	void load_user_scale()
	{
		std::ifstream file(USER_SCALE_FILE);
		float value = 1.0f;
		if (file && (file >> value) && value >= 0.5f && value <= 2.0f)
			g_user_scale = value;
	}

	void save_user_scale()
	{
		std::ofstream file(USER_SCALE_FILE);
		if (file)
			file << g_user_scale;
	}

	float clamp_scale(float scale)
	{
		return std::clamp(scale, UIConfig::UI_SCALE_MIN, UIConfig::UI_SCALE_MAX);
	}

	// Шаг 0.25: глифы чётче на "круглых" размерах, и мелкие колебания
	// измерений не заставляют пересобирать атлас шрифтов.
	float round_to_quarter(float scale)
	{
		return std::round(scale * 4.0f) / 4.0f;
	}

	/// Монитор, на котором лежит большая часть окна (окно может висеть
	/// между двумя мониторами). nullptr — не нашли, возьмём primary.
	GLFWmonitor* monitor_under_window(GLFWwindow* window)
	{
		int win_x = 0, win_y = 0, win_w = 0, win_h = 0;
		glfwGetWindowPos(window, &win_x, &win_y);
		glfwGetWindowSize(window, &win_w, &win_h);

		int monitor_count = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
		GLFWmonitor* best = nullptr;
		int best_overlap = 0;

		for (int i = 0; i < monitor_count; ++i)
		{
			const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
			if (!mode) continue;
			int mon_x = 0, mon_y = 0;
			glfwGetMonitorPos(monitors[i], &mon_x, &mon_y);

			const int overlap_w = std::min(win_x + win_w, mon_x + mode->width)  - std::max(win_x, mon_x);
			const int overlap_h = std::min(win_y + win_h, mon_y + mode->height) - std::max(win_y, mon_y);
			const int overlap   = std::max(overlap_w, 0) * std::max(overlap_h, 0);
			if (overlap > best_overlap) { best_overlap = overlap; best = monitors[i]; }
		}
		return best ? best : glfwGetPrimaryMonitor();
	}

	/// Масштаб из физической плотности монитора: PPI / UI_REFERENCE_PPI.
	/// 0.0 — размер в EDID отсутствует или мусорный (телевизоры, KVM),
	/// тогда полагаемся только на системный масштаб.
	float physical_scale_of(GLFWmonitor* monitor)
	{
		if (!monitor) return 0.0f;
		int width_mm = 0, height_mm = 0;
		glfwGetMonitorPhysicalSize(monitor, &width_mm, &height_mm);
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		if (!mode || width_mm < 50) return 0.0f; // < 5 см шириной = мусор EDID

		const float width_inches = (float)width_mm / 25.4f;
		const float ppi = (float)mode->width / width_inches;
		return ppi / UIConfig::UI_REFERENCE_PPI;
	}

	/// Пересчитывает итоговый масштаб из всех источников и взводит флаг,
	/// если он изменился. Единственное место, где итог собирается.
	void recompute_effective_scale()
	{
		if (!g_window) return;
		g_physical_scale = physical_scale_of(monitor_under_window(g_window));

		// ОС знает предпочтения пользователя, железо — реальную плотность.
		// Берём максимум: на ноутбуке с заниженной настройкой Windows текст
		// всё равно останется физически читаемым.
		const float base = std::max(g_os_scale, g_physical_scale);
		const float next = clamp_scale(round_to_quarter(base) * g_user_scale);

		if (next != g_effective_scale)
		{
			std::cout << "[UI-SCALE] Scale " << g_effective_scale << " -> " << next
			          << " (os " << g_os_scale
			          << ", physical " << g_physical_scale
			          << ", user " << g_user_scale << ")" << std::endl;
			g_effective_scale = next;
			g_scale_changed   = true;
		}
	}

	/// Колбэк GLFW: окно перетащили на монитор с другим системным DPI.
	void on_content_scale_changed(GLFWwindow* /*window*/, float x_scale, float /*y_scale*/)
	{
		g_os_scale = x_scale;
		recompute_effective_scale();
	}
}

namespace ui_scale
{
	void init(GLFWwindow* window)
	{
		if (!window) return;
		g_window = window;

		float x_scale = 1.0f, y_scale = 1.0f;
		glfwGetWindowContentScale(window, &x_scale, &y_scale);
		g_os_scale = x_scale;

		load_user_scale();
		glfwSetWindowContentScaleCallback(window, on_content_scale_changed);
		recompute_effective_scale();
		g_next_poll_time = std::chrono::steady_clock::now() + POLL_INTERVAL;
		std::cout << "[UI-SCALE] Initial scale: " << g_effective_scale << std::endl;
	}

	void poll(GLFWwindow* window)
	{
		if (!window) return;
		g_window = window;
		const auto now = std::chrono::steady_clock::now();
		if (now < g_next_poll_time) return;
		g_next_poll_time = now + POLL_INTERVAL;
		recompute_effective_scale();
	}

	float get()
	{
		return g_effective_scale;
	}

	float points(float logical_points)
	{
		return logical_points * g_effective_scale;
	}

	bool consume_scale_changed()
	{
		const bool changed = g_scale_changed;
		g_scale_changed = false;
		return changed;
	}

	void set_user_scale(float multiplier)
	{
		g_user_scale = std::clamp(multiplier, 0.5f, 2.0f);
		save_user_scale();
		recompute_effective_scale();
	}

	float user_scale()
	{
		return g_user_scale;
	}
}
