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
	// 30 FPS, и на 240 FPS. ВАЖНО: интервал обязан быть КОРОЧЕ COMMIT_DELAY —
	// тогда одиночное мусорное чтение от Windows (переходные состояния при
	// пробуждении/переключении дисплея) не доживает до коммита: следующий
	// опрос успевает его опровергнуть.
	constexpr auto POLL_INTERVAL = std::chrono::milliseconds(500);

	// Дебаунс: пока окно тащат между мониторами, системный DPI и физический
	// PPI меняются НЕ одновременно — цель скачет (2.25 -> 1 -> 2.25...), и
	// каждый скачок пересобирал бы шрифты. Применяем новый масштаб только
	// после того, как цель продержалась стабильной это время И подтверждена
	// минимум двумя замерами подряд — как делает macOS.
	constexpr auto COMMIT_DELAY = std::chrono::milliseconds(700);
	constexpr int  COMMIT_MIN_CONFIRMS = 2;

	// Настройка "UI Scale" из меню View переживает перезапуск.
	constexpr const char* USER_SCALE_FILE = "ui_scale.ini";

	GLFWwindow* g_window     = nullptr;
	float g_os_scale         = 1.0f;  // от ОС: настройка масштаба Windows
	float g_physical_scale   = 0.0f;  // от железа: PPI монитора / REFERENCE_PPI (0 = неизвестен)
	float g_user_scale       = 1.0f;  // множитель пользователя поверх всего
	float g_effective_scale  = 1.0f;  // итог, под который собраны шрифты
	bool  g_scale_changed    = false; // взводится при смене итога, гасится consume_*()
	float g_committed_monitor = 1.0f; // мониторная часть применённого масштаба
	float g_pending_scale    = 0.0f;  // кандидат, ждущий стабильности (0 = нет)
	float g_pending_monitor  = 1.0f;  // мониторная часть кандидата
	int   g_pending_confirms = 0;     // сколько замеров подряд дали кандидата
	std::chrono::steady_clock::time_point g_pending_since{};
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

	/// Считает целевой масштаб из всех источников и ставит его кандидатом.
	/// НЕ применяет сразу — коммит делает try_commit_pending() после того,
	/// как кандидат продержится COMMIT_DELAY (защита от дребезга при
	/// перетаскивании окна между мониторами).
	void update_target_scale()
	{
		if (!g_window) return;
		g_physical_scale = physical_scale_of(monitor_under_window(g_window));

		// ОС знает предпочтения пользователя, железо — реальную плотность.
		// Берём максимум: на ноутбуке с заниженной настройкой Windows текст
		// всё равно останется физически читаемым.
		const float monitor = round_to_quarter(std::max(g_os_scale, g_physical_scale));
		const float target  = clamp_scale(monitor * g_user_scale);

		if (target == g_effective_scale)
		{
			g_pending_scale = 0.0f; // вернулись к текущему — отменяем кандидата
			g_pending_confirms = 0;
			return;
		}
		if (target != g_pending_scale)
		{
			g_pending_scale   = target; // новый кандидат — таймер и счёт заново
			g_pending_monitor = monitor;
			g_pending_since = std::chrono::steady_clock::now();
			g_pending_confirms = 1;
		}
		else
		{
			++g_pending_confirms;     // тот же кандидат подтверждён ещё раз
		}
	}

	void commit_pending_now(); // определена ниже, у колбэка

	/// Опросный путь (косвенные замеры): кандидат должен продержаться
	/// COMMIT_DELAY и быть подтверждён COMMIT_MIN_CONFIRMS замерами подряд.
	void try_commit_pending()
	{
		if (g_pending_scale == 0.0f) return;
		if (g_pending_confirms < COMMIT_MIN_CONFIRMS) return;
		if (std::chrono::steady_clock::now() - g_pending_since < COMMIT_DELAY) return;
		commit_pending_now();
	}

	/// Немедленный коммит кандидата — для авторитетных источников (событие
	/// от ОС, выбор в меню, старт приложения), где подтверждений ждать нечего.
	void commit_pending_now()
	{
		if (g_pending_scale == 0.0f) return;
		std::cout << "[UI-SCALE] Scale " << g_effective_scale << " -> " << g_pending_scale
		          << " (os " << g_os_scale
		          << ", physical " << g_physical_scale
		          << ", user " << g_user_scale << ")" << std::endl;
		g_effective_scale   = g_pending_scale;
		g_committed_monitor = g_pending_monitor;
		g_pending_scale     = 0.0f;
		g_pending_confirms  = 0;
		g_scale_changed     = true;
	}

	/// Колбэк GLFW: ОС сообщила, что окно легло на монитор с другим DPI.
	/// Это не косвенный замер, а прямое событие Windows — применяем сразу,
	/// без дебаунса: пользователь перенёс окно и ждёт мгновенной реакции.
	void on_content_scale_changed(GLFWwindow* /*window*/, float x_scale, float /*y_scale*/)
	{
		g_os_scale = x_scale;
		update_target_scale();
		commit_pending_now();
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

		// На старте — без дебаунса: шрифты должны собраться под правильный
		// масштаб до первого кадра.
		update_target_scale();
		commit_pending_now();
		g_next_poll_time = std::chrono::steady_clock::now() + POLL_INTERVAL;
		std::cout << "[UI-SCALE] Initial scale: " << g_effective_scale << std::endl;
	}

	void poll(GLFWwindow* window)
	{
		if (!window) return;
		g_window = window;

		// Полный пересчёт (enum мониторов) — раз в секунду; проверка
		// "кандидат дозрел?" — дешёвая, каждый кадр.
		const auto now = std::chrono::steady_clock::now();
		if (now >= g_next_poll_time)
		{
			g_next_poll_time = now + POLL_INTERVAL;
			update_target_scale();
		}
		try_commit_pending();
	}

	float get()
	{
		return g_effective_scale;
	}

	float monitor_scale()
	{
		return g_committed_monitor;
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

		// Явное действие пользователя в меню — применяем сразу, без дебаунса.
		update_target_scale();
		commit_pending_now();
	}

	float user_scale()
	{
		return g_user_scale;
	}
}
