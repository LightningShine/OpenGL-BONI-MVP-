#pragma once
#include "../input/Input.h"
#include "../network/Server.h"
#include "../Config.h"
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

extern int g_focused_vehicle_id;  // -1 = лидер (дефолт), иначе ID машины
extern bool g_show_vehicle_names; // true = show TLA names above vehicles

// Зафиксированное пересечение линии старт/финиша.
//
// Геометрия проверяется ТАМ, ГДЕ РОЖДАЕТСЯ ОТРЕЗОК — на приёме пакета: только
// там видна каждая пара точек. Кадр рендера приходит реже пакетов (50 Гц на
// машину против 30-60 кадров на всех) и раньше видел лишь последнюю пару,
// поэтому часть пересечений терялась и круг засчитывался лишь со следующего
// проезда. При свёрнутом окне кадров нет вовсе, и терялись все.
//
// RaceManager разбирает накопленные события и ведёт по ним гоночную логику.
struct LineCrossing
{
	float    fraction = 0.0f;        // доля отрезка, на которой легла линия
	uint32_t utc_ms = 0;             // момент пересечения по метке источника
	bool     has_source_time = false;
	bool     from_geometry = true;   // false — резервная детекция по прогрессу
	// Машина побывала на дальней половине круга с прошлого зачёта, то есть это
	// настоящий круг, а не дребезг у линии. Взвод ведётся на приёме пакетов —
	// только там видно каждое значение прогресса.
	bool     armed = false;
};

struct LapData
{
	float lapTime;                              // Lap time in seconds
	int positionAtFinish;                       // Position when crossing line
	std::vector<glm::vec2> telemetryPoints;     // Placeholder for future telemetry
	
	LapData() : lapTime(0.0f), positionAtFinish(0) {}
	LapData(float time, int position) : lapTime(time), positionAtFinish(position) {}
};


struct LapInfo
{
	float timefromstart;
	double progress;
	std::chrono::steady_clock::time_point timestamp;
	double total_progress;
	float gForceX, gForceY;
	float aceleration, speed;
	int curentPosition;
	
};

struct CarLapSessions
{
	int lapnumber;
	int globalLapnumber;
	std::vector<LapInfo> samples;
};




class Vehicle
{
public:
	Vehicle();
	Vehicle(double normalized_x, double normalized_y);
	Vehicle(int32_t race_id, double normalized_x, double normalized_y);
	// race_id передаётся явно: пакет несёт ID железки, а не номер в гонке.
	// Раньше конструктор брал ID из пакета и машина ложилась в g_vehicles под
	// другим ключом — отсюда чистились не те записи при удалении.
	Vehicle(int32_t race_id, const TelemetryPacket& packet);
	
	double m_lat_dd;
	double m_lon_dd;
	double m_meters_easting = 0;
	double m_meters_northing = 0;
	double m_normalized_x = 0;
	double m_normalized_y = 0;
	double m_speed_kph;
	double m_acceleration;
	double m_g_force_x;
	double m_g_force_y;
	int16_t m_fix_type;
	// Номер участника в сессии (1..99). Он же — ключ в g_vehicles и во всех
	// сопутствующих картах (интерполятор, тайм-синк). Другого смысла не имеет.
	int32_t m_id = 0;
	// ID железки из пакета телеметрии (0 = машина создана не из телеметрии).
	// Реестр устройств и имена привязаны к нему, номер в гонке — к m_id.
	int32_t m_device_id = 0;
	std::string name = "Unknown";
	std::chrono::steady_clock::time_point m_last_update_time = std::chrono::steady_clock::now();
	glm::vec3 m_cached_color; 
	bool m_is_leader = false;  
	
	// ========================================================================
	// RENDERING CACHE (for smooth triangle rotation)
	// ========================================================================
	mutable float m_last_rotation_angle = 0.0f;
	
	// ========================================================================
	// LAP TIMING DATA (RaceManager reads/writes, Vehicle stores)
	// ========================================================================
	std::map<int, LapData> m_laps;
	float m_current_lap_timer = 0.0f;
	int m_current_lap_number = RaceConstants::LAP_START_NUMBER;
	int m_completed_laps = 0;
	double m_total_progress = 0.0;
	bool m_has_started_first_lap = false;
	float m_best_lap_time = -1.0f;
	int bestlapID = -1;
	bool m_is_finished = false;

	// Разница НОМЕРОВ кругов с лидером — то, что показывает колонка таблицы.
	// Лидер ушёл на следующий круг, а машина ещё на предыдущем — это уже «+1
	// круг», как на табло в большом автоспорте. От показа спасает порог по
	// времени: пока разрыв меньше LAP_GAP_DISPLAY_SECONDS, выводятся секунды.
	int  m_laps_behind_leader = 0;

	// Полных кругов ДИСТАНЦИИ позади лидера. Не зависит от того, кто когда
	// пересёк линию, поэтому именно эта мера ловит момент обгона на круг.
	int  m_distance_laps_behind = 0;

	// Реально отстал на круг дистанции — по этой величине пишется протокол.
	bool m_is_lapped = false;

	// Круг засчитывается только если машина побывала на дальней половине трассы
	// с момента прошлого зачёта. Защита от дублей по пространству, а не по
	// времени: закрывает и дребезг у самой линии, и скачок прогресса там, где
	// трасса подходит близко сама к себе.
	bool m_lap_armed = false;


	// ========================================================================
	// POSITION TRACKING (for line crossing detection)
	// ========================================================================
	double m_prev_x = 0.0;
	double m_prev_y = 0.0;
	double m_heading = 0.0;  // ✅ Current direction in radians

	// Метки времени источника для двух последних пакетов — gps_utc_ms, мс от
	// полуночи UTC, общие для всех трекеров (приходят из GNSS). На них считается
	// время круга: в отличие от кадрового таймера они не зависят ни от частоты
	// кадров, ни от скорости воспроизведения, поэтому живой заезд и повтор
	// записи дают один и тот же результат.
	uint32_t m_prev_packet_utc_ms = 0;
	uint32_t m_packet_utc_ms = 0;
	// false — источник своего времени не даёт (симуляция по клавише T пишет
	// позиции напрямую): для него хронометраж остаётся кадровым.
	bool m_has_source_time = false;
	// Метка пересечения линии, от которой идёт текущий круг.
	uint32_t m_lap_start_utc_ms = 0;

	// Метка ПЕРВОГО пакета машины. По ней проверяется «машина уже какое-то
	// время едет», а не по кадровому таймеру: при пересчёте повтора кадров нет
	// вовсе, и кадровая проверка отбрасывала первое пересечение, теряя круги.
	uint32_t m_first_packet_utc_ms = 0;

	// Пересечения, найденные на приёме пакетов и ещё не разобранные
	// хронометражем. Копится сетевым потоком, вычерпывается RaceManager.
	std::vector<LineCrossing> m_pending_crossings;

	// Пакетов нет дольше таймаута. Во время сессии такую машину не удаляем:
	// вместе с объектом умерли бы её круги (см. g_race_session_active).
	bool m_signal_lost = false;

	// ========================================================================
	// TRACK PROGRESS (0.0 = start, 1.0 = full lap)
	// ========================================================================
	double m_track_progress = 0.0;
	double m_prev_track_progress = 0.0;

	// Сегмент трека, на который спроецировалась машина в прошлый раз. Подсказка
	// для поиска ближайшего сегмента: между пакетами машина смещается на метры,
	// поэтому просматривать весь трек заново незачем.
	size_t m_track_segment_hint = 0;
	bool m_has_authoritative_state = false;
	bool m_apply_track_render_offset = true;
	// Race position computed by the Track Server (0 = none). When set, the
	// leaderboard uses IT instead of local progress — this is what freezes the
	// order after the checkered flag (post-finish overtakes must not count).
	int m_server_position = 0;
	
	// ========================================================================
	// FUTURE: Detailed telemetry history
	// ========================================================================
	std::map<int, CarLapSessions> laps;

	// Accumulator for throttling telemetry sample recording (seconds)
	float m_telemetry_sample_timer = 0.0f;
	
	// ========================================================================
	// COLOR GENERATION
	// ========================================================================
	glm::vec3 getColor() const;
};


extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;
extern std::atomic<bool> g_is_vehicles_active;

// Сессия запущена (Active/Finishing/Ended). Пока true, removeVehicles() не
// удаляет машины по таймауту — участник, потерявший связь, не должен терять
// круги. Флаг пишет RaceManager, чтобы vehicle-модуль не зависел от racing.
extern std::atomic<bool> g_race_session_active;

// Идёт откат повтора: состояние машин перестраивается прогоном записи от
// ключевого кадра к цели. Пока флаг поднят, ПОКАЗЫВАТЬ его нельзя — иначе
// видно, как машина прыгает назад к снимку и едет обратно, а таблица
// пересчитывается на лету. Потребители держат последний целый кадр.
// Пишет проигрыватель повтора; здесь — чтобы vehicle и racing не зависели
// от сетевого модуля.
extern std::atomic<bool> g_pipeline_rebuilding;

// ============================================================================
// Блокировка состояния машин.
//
// Обычный читатель берёт g_vehicles_mutex напрямую. А пересчёт повтора обязан
// удержать его на ВЕСЬ прогон: иначе рендер и панели читают состояние посреди
// перестройки — машины прыгают, таймер скачет. Заморозка отдельных
// потребителей эту задачу не решает: читателей состояния больше десятка, и
// забыть нового — вопрос времени.
//
// Приём пакета внутри такого прогона не имеет права брать мьютекс повторно,
// поэтому захват считается по потоку.
//
// Порядок захвата: g_vehicles_mutex берётся ПЕРВЫМ — до привязок устройств и
// до мьютекса геометрии трека. Обратный порядок недопустим.
// ============================================================================
class VehiclesLock
{
public:
	VehiclesLock();
	~VehiclesLock();
	VehiclesLock(const VehiclesLock&) = delete;
	VehiclesLock& operator=(const VehiclesLock&) = delete;
private:
	bool owns_;
};

// Захват на длинный пакетный участок (пересчёт повтора). Вызовы обязаны быть
// парными; внутри участка VehiclesLock не блокирует.
void enter_vehicles_bulk_section();
void leave_vehicles_bulk_section();









// Снимок машины для отрисовки: только то, что реально нужно кадру.
// Копировать Vehicle целиком нельзя — внутри лежат карты кругов со всей
// накопленной телеметрией (сэмпл каждые 0.1 с на машину), и такая копия
// на каждый кадр под g_vehicles_mutex съедала кадры тем сильнее, чем дольше
// шла сессия.
struct VehicleRenderState
{
	int32_t     id = 0;
	double      x = 0.0;
	double      y = 0.0;
	double      heading = 0.0;
	double      speed_kph = 0.0;
	glm::vec3   color{ 1.0f, 1.0f, 1.0f };
	std::string name;
	bool        is_leader = false;
	bool        apply_track_render_offset = true;
};

// === Function ===
void vehicleLoop(); // Главный цикл обновления машин

int32_t generateVehicleID();

std::vector<glm::vec2> generateCircle(float radius, int segments = 16);
std::vector<glm::vec2> generateTriangle(float size); // ✅ Треугольник для лидера

// camera_zoom: the marker is scaled by 1/zoom so it keeps a constant
// on-screen size instead of growing when the user zooms into the track.
void renderVehicle(GLuint shader_program, GLuint vao, GLuint vbo,
	const VehicleRenderState& vehicle, const glm::mat4& projection, float camera_zoom = 1.0f);

void renderAllVehicles(GLuint shader_program, GLuint vao, GLuint vbo,
	const glm::mat4& projection,
	const glm::vec2& camera_pos, float camera_zoom);

void removeVehicles();

void vehicleClose();