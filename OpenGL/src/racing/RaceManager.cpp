#include "RaceManager.h"
#include "LapClock.h"
#include "../core/AppPaths.h"
#include "../core/WorldSnapshot.h"
#include "../vehicle/Vehicle.h"
#include "../rendering/Interpolation.h"
#include "../Config.h"
#include "TimeDiffirence/TimeDiff.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <sstream>

// ============================================================================
// EXTERNAL GLOBALS
// ============================================================================
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;
extern std::vector<SplinePoint> g_smooth_track_points;
extern std::atomic<bool> g_is_map_loaded;

// ============================================================================
// GLOBAL RACE MANAGER INSTANCE
// ============================================================================
RaceManager* g_race_manager = nullptr;

namespace {
    using racing::utc_at_fraction;
    using racing::utc_elapsed_ms;

    // Круг длиннее часа — почти наверняка сбитая метка, а не медленная машина.
    // В таком случае честнее откатиться на кадровый таймер, чем записать мусор.
    constexpr float MAX_PLAUSIBLE_LAP_SECONDS = 3600.0f;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================
RaceManager::RaceManager()
    : m_lineInitialized(false)
    , m_startFinishP1(0.0f, 0.0f)
    , m_startFinishP2(0.0f, 0.0f)
{
    std::cout << "[RACE MANAGER] Initialized" << std::endl;
}

RaceManager::~RaceManager()
{
    std::cout << "[RACE MANAGER] Destroyed" << std::endl;
}

// ============================================================================
// SET START/FINISH LINE
// ============================================================================
void RaceManager::SetStartFinishLine(const glm::vec2& p1, const glm::vec2& p2)
{
    m_startFinishP1 = p1;
    m_startFinishP2 = p2;
    m_lineInitialized = true;

    std::cout << "[RACE MANAGER] Start/Finish line set: "
              << "P1(" << p1.x << ", " << p1.y << ") -> "
              << "P2(" << p2.x << ", " << p2.y << ")" << std::endl;
}

bool RaceManager::GetStartFinishLine(glm::vec2& out_p1, glm::vec2& out_p2) const
{
    if (!m_lineInitialized)
        return false;

    out_p1 = m_startFinishP1;
    out_p2 = m_startFinishP2;
    return true;
}

// ============================================================================
// AUTO STOP
// ============================================================================
void RaceManager::SetAutoStopConditions(int maxLaps, float maxSeconds)
{
    m_autoStopMaxLaps = maxLaps;
    m_autoStopMaxSeconds = maxSeconds;
}

int RaceManager::GetAutoStopLaps() const
{
    return m_autoStopMaxLaps;
}

float RaceManager::GetAutoStopSeconds() const
{
    return m_autoStopMaxSeconds;
}

// ============================================================================
// CORE UPDATE LOOP - Reads/writes Vehicle data directly under mutex
// ============================================================================
void RaceManager::Update(float deltaTime)
{
    if (!g_is_map_loaded || !m_lineInitialized)
        return;

    if (m_sessionState == SessionState::Ended)
        return;

    if (m_sessionState == SessionState::Active)
    {
        bool timeHit = (m_autoStopMaxSeconds > 0.0f && GetRaceElapsedTime() >= m_autoStopMaxSeconds);
        bool lapHit = false;

        if (m_autoStopMaxLaps > 0)
        {
            for (const auto& [id, veh] : g_vehicles)
            {
                if (veh.m_completed_laps >= m_autoStopMaxLaps)
                {
                    lapHit = true;
                    break;
                }
            }
        }

        if (timeHit || lapHit)
        {
            StopSession();
        }
    }

    VehiclesLock lock;

    for (auto& [vehicleID, vehicle] : g_vehicles)
    {
        constexpr bool kDebugFinishCrossing = true;
        // ====================================================================
        // TELEMETRY RECORDING (every frame during active lap)
        // Records vehicle state for TimeDiff calculations.
        // IMPORTANT: On clients, vehicles can be authoritative (replicated)
        // and we still need to record samples, otherwise TimeDiff can't work.
        // ====================================================================
        if (vehicle.m_has_started_first_lap && !vehicle.m_is_finished)
        {
            constexpr float kTelemetrySampleInterval = 0.1f; // 10 Hz
            constexpr size_t kMaxSamplesPerLap = 36000;       // 1 hour cap per lap
            vehicle.m_telemetry_sample_timer += deltaTime;

            if (vehicle.m_telemetry_sample_timer >= kTelemetrySampleInterval)
            {
                vehicle.m_telemetry_sample_timer = 0.0f;

                LapInfo sample;
                sample.timefromstart = vehicle.m_current_lap_timer;
                sample.progress = vehicle.m_track_progress;
                sample.timestamp = std::chrono::steady_clock::now();
                sample.total_progress = vehicle.m_total_progress;
                sample.gForceX = static_cast<float>(vehicle.m_g_force_x);
                sample.gForceY = static_cast<float>(vehicle.m_g_force_y);
                sample.aceleration = static_cast<float>(vehicle.m_acceleration);
                sample.speed = static_cast<float>(vehicle.m_speed_kph);
                sample.curentPosition = 0; // Updated after standings sort

                if (vehicle.laps.find(vehicle.m_current_lap_number) == vehicle.laps.end())
                {
                    vehicle.laps[vehicle.m_current_lap_number] = CarLapSessions();
                    vehicle.laps[vehicle.m_current_lap_number].lapnumber = vehicle.m_current_lap_number;
                }
                auto& currentLapSamples = vehicle.laps[vehicle.m_current_lap_number].samples;

                // Повтор отмотали назад и поехали заново по тому же участку:
                // всё, что записано ДАЛЬШЕ текущего места, относится к проходу,
                // которого на этой точке ещё не было. Отбрасываем его здесь, при
                // перезаписи, а не при самой перемотке: пока оператор просто
                // мотает туда-сюда, история должна оставаться целой, иначе на
                // одной и той же точке панель секторов показывает каждый раз
                // разное время.
                //
                // Заодно это держит главный инвариант истории — прогресс внутри
                // круга возрастает. На нём стоит и двоичный поиск в
                // visibleSampleCount, и расчёт секторов по running-max.
                while (!currentLapSamples.empty() &&
                       currentLapSamples.back().progress > sample.progress)
                {
                    currentLapSamples.pop_back();
                }

                if (currentLapSamples.size() < kMaxSamplesPerLap)
                    currentLapSamples.push_back(sample);
            }
        }

        // Vehicles driven by processed server state already have authoritative
        // lap/progress/timing values. Do not advance them locally on the client.
        if (vehicle.m_has_authoritative_state)
        {
            // Сервер шлёт lap_time в state-сообщениях реже, чем идут кадры —
            // без локального тика время на экране прыгало бы ступеньками.
            // Тикаем сами, сервер каждым сообщением поправляет накопившийся
            // дрейф (см. TrackServerClient: значение назад не откатывается).
            if (vehicle.m_has_started_first_lap && !vehicle.m_is_finished)
                vehicle.m_current_lap_timer += deltaTime;

            // Client-side: do not advance timing/lap counters locally.
            // Still keep derived fields consistent for standings/time-diff.
            // If server provides a valid best lap time, keep it visible in UI.
            if (vehicle.m_best_lap_time >= 0.0f)
            {
                // Best lap ID is used by TimeDiff; if it is unset, point it to the
                // latest completed lap (bestlap time itself is authoritative).
                if (vehicle.bestlapID < RaceConstants::LAP_START_NUMBER)
                {
                    const int candidate = vehicle.m_current_lap_number - 1;
                    if (candidate >= RaceConstants::LAP_START_NUMBER)
                        vehicle.bestlapID = candidate;
                }
            }

            vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
            continue;
        }
        
        // Геометрию пересечения здесь больше не считаем: она вычисляется на
        // приёме пакета, где видна каждая пара точек (см. LineCrossing).
        // Забираем пересечения, найденные приёмом телеметрии. Он видит КАЖДЫЙ
        // отрезок движения, поэтому здесь разбираются все проезды подряд — в
        // том числе накопившиеся, пока окно было свёрнуто и кадров не было.
        std::vector<LineCrossing> crossings;
        crossings.swap(vehicle.m_pending_crossings);

        if (kDebugFinishCrossing)
        {
            for (const LineCrossing& crossing : crossings)
            {
                std::cout.setf(std::ios::fixed);
                std::cout << "[S/F DEBUG] veh#" << vehicleID
                          << " sessionState=" << static_cast<int>(m_sessionState)
                          << " finished=" << (vehicle.m_is_finished ? 1 : 0)
                          << " geometry=" << (crossing.from_geometry ? 1 : 0)
                          << " armed=" << (crossing.armed ? 1 : 0)
                          << " ratio=" << std::setprecision(3) << crossing.fraction
                          << std::endl;
            }
        }

        if (m_sessionState == SessionState::Idle)
        {
            // Just riding, reset timer if crossed but don't record laps.
            if (!crossings.empty())
                vehicle.m_current_lap_timer = 0.0f;
            else
                vehicle.m_current_lap_timer += deltaTime;
            vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
            continue;
        }

        if (vehicle.m_is_finished)
        {
            // Just driving after finishing. Ignore laps.
            vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
            continue;
        }

        if (crossings.empty())
        {
            vehicle.m_current_lap_timer += deltaTime;
        }

        for (const LineCrossing& crossing : crossings)
        {
        const float intersectionRatio = crossing.fraction;
        const bool crossed = crossing.from_geometry;

        // --------------------------------------------------------------------
        // ПРОМЕЖУТОЧНАЯ ТОЧКА ЗАМЕРА
        // Закрывает предыдущий сектор разностью меток и открывает следующий.
        // Круг здесь не трогаем — им занимается точка 0 ниже.
        // --------------------------------------------------------------------
        if (crossing.point_index != 0)
        {
            // Секторы считаем только внутри боевого круга: до первого проезда
            // линии отсчитывать не от чего.
            if (vehicle.m_has_started_first_lap && vehicle.m_sector_start_utc_ms != 0 &&
                crossing.has_source_time)
            {
                const int closed = crossing.point_index - 1;
                if (closed >= 0 && closed < SECTOR_COUNT)
                {
                    const float measured =
                        utc_elapsed_ms(vehicle.m_sector_start_utc_ms, crossing.utc_ms) / 1000.0f;

                    // Отрицательное или неправдоподобное время — это сбой меток,
                    // а не быстрый сектор. Лучше показать прочерк, чем число,
                    // которое пойдёт в рекорды.
                    if (measured > 0.0f && measured < MAX_PLAUSIBLE_LAP_SECONDS)
                        vehicle.m_current_lap_sectors[closed] = measured;
                }

                vehicle.m_sector_start_utc_ms = crossing.utc_ms;
                vehicle.m_current_sector = crossing.point_index;
            }
            continue;
        }

        if (vehicle.m_has_started_first_lap && crossing.armed)
        {

            // ----------------------------------------------------------------
            // ВРЕМЯ КРУГА
            // Основной источник — метки времени пакетов: разность gps_utc_ms
            // между двумя пересечениями. Доля отрезка берётся из геометрии
            // (где именно на отрезке легла линия), а не из длительности кадра.
            // Поэтому результат не зависит ни от FPS, ни от скорости
            // воспроизведения записи — живой заезд и повтор совпадают.
            //
            // Кадровый таймер остаётся запасным путём: у источников без
            // собственного времени (симуляция по клавише T) меток просто нет.
            // ----------------------------------------------------------------
            const float crossing_fraction = intersectionRatio;
            float crossingTime = vehicle.m_current_lap_timer + (deltaTime * crossing_fraction);
            const uint32_t crossing_utc_ms = crossing.utc_ms;
            const bool crossing_utc_valid = crossing.has_source_time;

            if (crossing_utc_valid)
            {
                if (vehicle.m_lap_start_utc_ms != 0)
                {
                    const float measured =
                        utc_elapsed_ms(vehicle.m_lap_start_utc_ms, crossing_utc_ms) / 1000.0f;
                    if (measured > 0.0f && measured < MAX_PLAUSIBLE_LAP_SECONDS)
                        crossingTime = measured;
                }
            }

            const float MIN_VALID_LAP_TIME = 0.1f;

            if (crossingTime > MIN_VALID_LAP_TIME)
            {
                // ----------------------------------------------------------------
                // In Finishing state, enforce correct finishing order:
                //   1) The stored leader must finish first.
                //   2) Same-lap cars may finish only after the leader has crossed.
                //   3) Lapped cars may finish only after ALL same-lap cars are done.
                // If a car is not yet allowed to finish, skip recording this
                // crossing entirely — the timer resets below and it tries again.
                // ----------------------------------------------------------------
                bool processLap = true;
                if (m_sessionState == SessionState::Finishing)
                {
                    const bool isTheLeader       = (vehicleID == m_leaderAtStop);
                    const bool leaderHasFinished = (m_leaderAtStop >= 0 &&
                                                    m_finishPositions.count(m_leaderAtStop) > 0);
                    // completed_laps not yet incremented here — compare against stored baseline
                    const bool isLeadLapCar      = (vehicle.m_completed_laps >= m_leaderLapsAtStop);
                    const bool allLeadLapFinished = (static_cast<int>(m_finishPositions.size()) >= m_leadLapCarCount);

                    processLap = (isTheLeader ||
                                  (isLeadLapCar  && leaderHasFinished) ||
                                  (!isLeadLapCar && allLeadLapFinished));
                }

                if (processLap)
                {
                    // Последний сектор закрывается самой линией: он идёт от
                    // предыдущей точки замера до зачёта круга. Поэтому секторы
                    // и дают в сумме время круга — это разбиение одного и того
                    // же отрезка, а не независимые измерения.
                    if (vehicle.m_sector_start_utc_ms != 0 && crossing_utc_valid)
                    {
                        const float measured =
                            utc_elapsed_ms(vehicle.m_sector_start_utc_ms, crossing_utc_ms) / 1000.0f;
                        if (measured > 0.0f && measured < MAX_PLAUSIBLE_LAP_SECONDS)
                            vehicle.m_current_lap_sectors[SECTOR_COUNT - 1] = measured;
                    }

                    // Store completed lap
                    LapData lapData(crossingTime, 0);
                    lapData.sectors = vehicle.m_current_lap_sectors;
                    vehicle.m_laps[vehicle.m_current_lap_number] = lapData;
                    vehicle.m_completed_laps++;

                    // Update best lap time and ID
                    if (crossingTime < vehicle.m_best_lap_time || vehicle.m_best_lap_time < 0.0f)
                    {
                        vehicle.m_best_lap_time = crossingTime;
                        vehicle.bestlapID = vehicle.m_current_lap_number;
                    }

                    // Секторы печатаем вместе с кругом: их сумма обязана
                    // сходиться с временем круга, и это видно прямо в журнале —
                    // расхождение означает потерянное пересечение, а не
                    // «панель что-то не так показала».
                    std::cout << "[RACE MANAGER] Vehicle #" << vehicleID
                              << " completed Lap " << vehicle.m_current_lap_number
                              << " in " << std::fixed << std::setprecision(3) << crossingTime << "s"
                              << " | sectors";
                    float sector_sum = 0.0f;
                    bool  all_measured = true;
                    for (int i = 0; i < SECTOR_COUNT; ++i)
                    {
                        const float t = lapData.sectors[i];
                        if (t > 0.0f) { std::cout << " " << t; sector_sum += t; }
                        else          { std::cout << " --"; all_measured = false; }
                    }
                    if (all_measured)
                        std::cout << " (sum " << sector_sum << ")";
                    std::cout << " | Total completed: " << vehicle.m_completed_laps << std::endl;

                    if (m_sessionState == SessionState::Finishing)
                    {
                        vehicle.m_is_finished = true;
                        if (m_finishPositions.find(vehicleID) == m_finishPositions.end())
                            m_finishPositions[vehicleID] = static_cast<int>(m_finishPositions.size() + 1);
                        vehicle.m_current_lap_timer = 0.0f;
                        std::cout << "[RACE MANAGER] Vehicle #" << vehicleID
                                  << " HAS FINISHED! pos=" << m_finishPositions[vehicleID] << std::endl;
                    }
                    else
                    {
                        vehicle.m_current_lap_number++;
                    }
                }
                // else: car not yet allowed to finish — timer resets below, it retries next crossing
            }

            // Reset timer after crossing (applies even when processLap==false).
            // Отсчёт нового круга начинается с ТОГО ЖЕ момента, что и зачёт
            // предыдущего: иначе доля отрезка между двумя пересечениями
            // потерялась бы и круги медленно расползались бы по времени.
            vehicle.m_current_lap_timer = deltaTime * (1.0f - crossing_fraction);
            if (crossing_utc_valid)
                vehicle.m_lap_start_utc_ms = crossing_utc_ms;

            // Новый круг — новый первый сектор, от того же момента.
            vehicle.m_current_lap_sectors.fill(SECTOR_TIME_NONE);
            vehicle.m_current_sector = 0;
            vehicle.m_sector_start_utc_ms = crossing_utc_valid ? crossing_utc_ms : 0;
        }
        // ====================================================================
        // FIRST LAP START DETECTION
        // ====================================================================
        else if (!vehicle.m_has_started_first_lap)
        {
            // Защита от ложного старта в момент создания машины: пересечение
            // засчитываем, только если машина едет уже заметное время.
            //
            // Меряем это ВРЕМЕНЕМ ИСТОЧНИКА, а не кадрами. При пересчёте после
            // перемотки назад кадров не происходит вовсе, кадровый таймер
            // оставался нулём — и первое пересечение каждой машины молча
            // выбрасывалось вместе со всеми её кругами.
            constexpr uint32_t MIN_TIME_BEFORE_FIRST_LAP_MS = 500;
            const bool driving_long_enough =
                vehicle.m_has_source_time
                    ? utc_elapsed_ms(vehicle.m_first_packet_utc_ms, vehicle.m_packet_utc_ms) >
                          MIN_TIME_BEFORE_FIRST_LAP_MS
                    : vehicle.m_current_lap_timer > 0.5f;

            if (driving_long_enough)
            {
                if (kDebugFinishCrossing)
                {
                    std::cout.setf(std::ios::fixed);
                    std::cout << "[S/F DEBUG] START veh#" << vehicleID
                              << " fixType=" << vehicle.m_fix_type
                              << " geometry=" << (crossed ? 1 : 0)
                              << " ratio=" << std::setprecision(3) << intersectionRatio
                              << " curProg=" << vehicle.m_track_progress
                              << " lapT=" << vehicle.m_current_lap_timer
                              << std::endl;
                }

                vehicle.m_has_started_first_lap = true;
                vehicle.m_current_lap_timer = deltaTime * (1.0f - intersectionRatio);
                vehicle.m_prev_track_progress = vehicle.m_track_progress;

                // Отсюда пойдёт отсчёт первого боевого круга. Момент берём тот
                // же, что и для зачёта: точку на отрезке, где легла линия.
                if (crossing.has_source_time)
                    vehicle.m_lap_start_utc_ms = crossing.utc_ms;

                // Первый сектор начинается там же, где и круг.
                vehicle.m_current_lap_sectors.fill(SECTOR_TIME_NONE);
                vehicle.m_current_sector = 0;
                vehicle.m_sector_start_utc_ms = crossing.has_source_time ? crossing.utc_ms : 0;


                std::cout << "[RACE MANAGER] Vehicle #" << vehicleID 
                          << " crossed start/finish line, starting Lap " << vehicle.m_current_lap_number 
                          << std::endl;
            }
            else
            {
                vehicle.m_current_lap_timer += deltaTime;
            }
        }
        // Пересечение без взвода у уже стартовавшей машины — дребезг у самой
        // линии, а не круг. Пропускаем.
        }  // for (crossings)

        // ====================================================================
        // ТАЙМЕР ТЕКУЩЕГО КРУГА НА ЭКРАНЕ
        // Привязан к часам источника, а не к кадрам: иначе после перемотки он
        // начинал бы отсчёт заново от нуля и показывал не то время, которое
        // машина реально имела в этой точке заезда. Метки пакетов идут 50 раз
        // в секунду, так что для глаза это по-прежнему плавно.
        // ====================================================================
        if (vehicle.m_has_source_time && vehicle.m_lap_start_utc_ms != 0 &&
            vehicle.m_has_started_first_lap && !vehicle.m_is_finished)
        {
            vehicle.m_current_lap_timer =
                utc_elapsed_ms(vehicle.m_lap_start_utc_ms, vehicle.m_packet_utc_ms) / 1000.0f;
        }

        // ====================================================================
        // UPDATE TOTAL PROGRESS (lap number + current lap progress)
        // This is essential for CalculateLeaderTimeDiffInternal to work
        // Total progress allows comparing vehicles on different laps
        // Example: Lap 2, progress 0.35 -> total_progress = 2.35
        // ====================================================================
        vehicle.m_total_progress = vehicle.m_completed_laps + vehicle.m_track_progress;
    }

    // Check if everyone finished
    if (m_sessionState == SessionState::Finishing) {
        bool allFinished = true;
        for (const auto& [id, veh] : g_vehicles) {
            if (veh.m_has_started_first_lap && !veh.m_is_finished) {
                allFinished = false;
                break;
            }
        }
        if (allFinished && !g_vehicles.empty()) {
            m_sessionState = SessionState::Ended;
            if (m_raceTimerRunning)
            {
                auto now = std::chrono::steady_clock::now();
                std::chrono::duration<float> elapsed = now - m_raceStartTime;
                m_raceElapsedSeconds = elapsed.count();
                m_raceTimerRunning = false;
            }
            std::cout << "[SESSION] Session Ended! All cars have finished." << std::endl;
        }
    }

    // Update leader and positions
    std::vector<VehicleStanding> standings = GetStandingsInternal();

    // ====================================================================
    // КРУГОВЫЕ
    // Считаем по реальному отставанию в дистанции, а не по счётчику кругов.
    // Со счётчиком статус переключался в момент, когда КТО-ТО ИЗ ДВОИХ
    // пересекал линию, а не когда лидер действительно настигал машину: поэтому
    // круговой не появлялся при обгоне на круг и не снимался, когда круговой
    // отыгрывался обратно. Здесь состояние согласовано (счётчики кругов уже
    // обновлены выше в этом же кадре), поэтому флаг ставим на саму машину.
    // ====================================================================
    if (!standings.empty())
    {
        const auto leader_it = g_vehicles.find(standings[0].vehicleID);
        if (leader_it != g_vehicles.end())
        {
            const Vehicle& leader = leader_it->second;
            const double leader_progress = leader.m_total_progress;
            const int leader_lap_number = leader.m_current_lap_number;

            for (auto& [id, vehicle] : g_vehicles)
            {
                if (!vehicle.m_has_started_first_lap)
                {
                    vehicle.m_laps_behind_leader = 0;
                    vehicle.m_distance_laps_behind = 0;
                    vehicle.m_is_lapped = false;
                    continue;
                }

                // Мера 1 — разница НОМЕРОВ кругов. Ловит момент, когда лидер
                // уходит на новый круг, а машина ещё на предыдущем.
                const int lap_number_gap = leader_lap_number - vehicle.m_current_lap_number;

                // Мера 2 — отставание по ДИСТАНЦИИ. Ловит момент физического
                // обгона на круг. Нужна отдельно, потому что сразу после того,
                // как отстающий пересёк линию, номера кругов у него и у лидера
                // сравниваются — хотя круг отставания никуда не делся.
                double gap_laps = leader_progress - vehicle.m_total_progress;
                if (gap_laps < 0.0)
                    gap_laps = 0.0;

                const int previous_distance = vehicle.m_distance_laps_behind;
                int distance_laps = static_cast<int>(std::floor(gap_laps));

                // Прибавляется сразу, снимается через зазор: иначе на границе
                // ровно одного круга значение мигало бы туда-обратно.
                if (distance_laps < previous_distance &&
                    gap_laps > static_cast<double>(previous_distance) - RaceConstants::LAPS_BEHIND_HYSTERESIS)
                {
                    distance_laps = previous_distance;
                }
                vehicle.m_distance_laps_behind = distance_laps;
                vehicle.m_is_lapped = (distance_laps >= 1);

                // В таблицу идёт большая из двух: метка обязана появиться и при
                // уходе лидера на новый круг, и при обгоне на круг — смотря что
                // произошло раньше, — и не пропадать, пока верно хоть одно.
                vehicle.m_laps_behind_leader =
                    (lap_number_gap > distance_laps) ? lap_number_gap : distance_laps;
                if (vehicle.m_laps_behind_leader < 0)
                    vehicle.m_laps_behind_leader = 0;
            }
        }
    }


    // ====================================================================
    // UPDATE CURRENT POSITION IN TELEMETRY SAMPLES
    // ====================================================================
    for (size_t i = 0; i < standings.size(); ++i)
    {
        auto veh_it = g_vehicles.find(standings[i].vehicleID);
        if (veh_it != g_vehicles.end())
        {
            auto& vehicle = veh_it->second;
            if (vehicle.m_has_started_first_lap && !vehicle.laps.empty())
            {
                auto lap_it = vehicle.laps.find(vehicle.m_current_lap_number);
                if (lap_it != vehicle.laps.end() && !lap_it->second.samples.empty())
                {
                    lap_it->second.samples.back().curentPosition = static_cast<int>(i + 1);
                }
            }
        }
    }
    
    if (!standings.empty())
    {
        // Включено намеренно: смена лидера — редкое событие, а без записи в
        // журнал расхождение между живым заездом и повтором приходится
        // угадывать. Строка печатается раз на смену, не в горячем пути.
        constexpr bool kLogLeaderChanges = true;
        static int32_t previousLeader = -1;
        int32_t currentLeader = standings[0].vehicleID;
        
        // Reset all leader flags
        for (auto& [id, vehicle] : g_vehicles)
        {
            vehicle.m_is_leader = false;
        }
        
        // Set current leader flag
        auto leader_it = g_vehicles.find(currentLeader);
        if (leader_it != g_vehicles.end())
        {
            leader_it->second.m_is_leader = true;
        }
        
        // Debug: print leader change
        if (kLogLeaderChanges && currentLeader != previousLeader && previousLeader != -1)
        {
            std::cout << "\n[LEADER CHANGE] New leader: Vehicle #" << currentLeader 
                      << " | Laps: " << standings[0].completedLaps 
                      << " | Progress: " << std::fixed << std::setprecision(3) << standings[0].distanceFromStart
                      << " (was Vehicle #" << previousLeader << ")" << std::endl;
            
            size_t topCount = standings.size() < 3 ? standings.size() : 3;
            for (size_t i = 0; i < topCount; ++i)
            {
                std::cout << "  " << (i+1) << ". Vehicle #" << standings[i].vehicleID 
                          << " | Laps: " << standings[i].completedLaps
                          << " | Progress: " << std::fixed << std::setprecision(3) << standings[i].distanceFromStart
                          << std::endl;
            }
            std::cout << std::endl;
        }
        
        previousLeader = currentLeader;
    }

    // ========================================================================
    // ПУБЛИКАЦИЯ СНИМКА
    // Состояние согласовано: пересечения разобраны, круги и таймеры пересчитаны,
    // таблица построена и отсортирована. Только теперь отдаём его наружу.
    // Интерфейс читает исключительно снимок и поэтому не может застать
    // половину перестройки — см. world/WorldSnapshot.h.
    // ========================================================================
    PublishSnapshot(standings);
}

void RaceManager::PublishPositionsOnly() const
{
    // Таблицу берём из прошлого снимка как есть: при перемотке порядок за
    // тридцать миллисекунд не меняется, а её пересчёт — самая дорогая часть.
    PublishSnapshot(world::current()->standings);
}

void RaceManager::PublishSnapshot(const std::vector<VehicleStanding>& standings) const
{
    static uint64_t s_revision = 0;

    auto snapshot = std::make_shared<world::Snapshot>();
    snapshot->standings = standings;
    snapshot->revision = ++s_revision;

    for (const auto& [id, vehicle] : g_vehicles)
    {
        world::VehicleView view;
        view.id = id;
        view.device_id = vehicle.m_device_id;
        view.name = vehicle.name;
        view.color = vehicle.m_cached_color;

        view.x = vehicle.m_normalized_x;
        view.y = vehicle.m_normalized_y;
        view.heading = vehicle.m_heading;
        view.speed_kph = vehicle.m_speed_kph;
        view.acceleration = vehicle.m_acceleration;
        view.g_force_x = vehicle.m_g_force_x;
        view.g_force_y = vehicle.m_g_force_y;
        view.track_progress = vehicle.m_track_progress;
        view.total_progress = vehicle.m_total_progress;
        view.apply_track_render_offset = vehicle.m_apply_track_render_offset;

        view.completed_laps = vehicle.m_completed_laps;
        view.current_lap_number = vehicle.m_current_lap_number;
        view.best_lap_id = vehicle.bestlapID;
        view.current_lap_timer = vehicle.m_current_lap_timer;
        view.best_lap_time = vehicle.m_best_lap_time;
        view.has_started_first_lap = vehicle.m_has_started_first_lap;
        view.is_leader = vehicle.m_is_leader;
        view.is_finished = vehicle.m_is_finished;
        view.is_lapped = vehicle.m_is_lapped;
        view.signal_lost = vehicle.m_signal_lost;

        view.fix_type = vehicle.m_fix_type;
        view.packet_utc_ms = vehicle.m_packet_utc_ms;
        view.laps = vehicle.m_laps;

        view.sectors = vehicle.m_current_lap_sectors;
        view.current_sector = vehicle.m_current_sector;
        // Сколько машина едет в текущем секторе — по меткам пакетов, тем же
        // часам, что и весь остальной хронометраж.
        view.current_sector_elapsed =
            (vehicle.m_sector_start_utc_ms != 0 && vehicle.m_has_started_first_lap)
                ? utc_elapsed_ms(vehicle.m_sector_start_utc_ms, vehicle.m_packet_utc_ms) / 1000.0f
                : 0.0f;

        snapshot->vehicles.emplace(id, std::move(view));
    }

    world::publish(std::move(snapshot));
}

// ============================================================================
// LINE SEGMENT INTERSECTION (2D)
// Returns true if segments intersect, and outIntersectionRatio (0.0 to 1.0)
// indicates where along the vehicle's path the intersection occurred.
// ============================================================================
    auto formatTime = [](float totalSeconds) {
        if (totalSeconds < 0.0f)
            totalSeconds = 0.0f;

        int minutes = static_cast<int>(totalSeconds) / 60;
        float seconds = std::fmod(totalSeconds, 60.0f);

        std::ostringstream ss;
        ss << minutes << ":" << std::setw(6) << std::setfill('0')
           << std::fixed << std::setprecision(3) << seconds;
        return ss.str();
    };

// ============================================================================
// GET STANDINGS (sorted leaderboard) - Internal version without mutex lock
// ============================================================================
std::vector<VehicleStanding> RaceManager::GetStandingsInternal() const
{
    std::vector<VehicleStanding> standings;
    const bool useFinishOrder = (m_sessionState == SessionState::Finishing || m_sessionState == SessionState::Ended);
    
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        VehicleStanding standing;
        standing.vehicleID = vehicleID;
        standing.completedLaps = vehicle.m_completed_laps;
        standing.currentLapNumber = vehicle.m_current_lap_number;
        standing.currentLapTime = vehicle.m_is_finished ? 0.0f : vehicle.m_current_lap_timer;
        standing.hasStartedFirstLap = vehicle.m_has_started_first_lap;
        standing.isFinished = vehicle.m_is_finished;
        standing.isLapped = vehicle.m_is_lapped;
        standing.lapsBehindLeader = vehicle.m_laps_behind_leader;
        standing.distanceFromStart = vehicle.m_track_progress;
        standing.serverPosition = vehicle.m_has_authoritative_state
                                    ? vehicle.m_server_position : 0;
        
        standing.bestLapTime = (vehicle.m_completed_laps >= RaceConstants::MIN_LAPS_FOR_BEST_LAP)
                                 ? vehicle.m_best_lap_time : -1.0f;
        
        // Calculate total race time (sum of all completed laps)
        standing.totalRaceTime = 0.0f;
        for (const auto& [lapNum, lapData] : vehicle.m_laps)
        {
            standing.totalRaceTime += lapData.lapTime;
        }
        
        // ====================================================================
        // CALCULATE TIME DIFFERENCES (using Internal versions - no mutex)
        // ====================================================================
        if (vehicle.m_is_finished || m_sessionState == SessionState::Ended)
        {
            standing.deltaTimeToBest = 0.0f;
            standing.deltaTimeToLeader = 0.0f;
        }
        else
        {
            standing.deltaTimeToBest = CalculateLapTimeDiffInternal(vehicleID);
            standing.deltaTimeToLeader = CalculateLeaderTimeDiffInternal(vehicleID);
        }
        
        standings.push_back(standing);
    }
    
    // ========================================================================
    // SORT: 0) Track Server position when authoritative, else
    //       1) Started racing? 2) Completed laps (desc), 3) Progress (desc)
    // ========================================================================
    std::sort(standings.begin(), standings.end(),
        [this, useFinishOrder](const VehicleStanding& a, const VehicleStanding& b) -> bool
        {
            // Networked session: the server's classification is the truth. It
            // freezes at the checkered flag, so a finished leader can never be
            // visually overtaken by live track progress after the line.
            if (a.serverPosition > 0 && b.serverPosition > 0)
                return a.serverPosition < b.serverPosition;
            if ((a.serverPosition > 0) != (b.serverPosition > 0))
                return a.serverPosition > 0;

            if (a.hasStartedFirstLap != b.hasStartedFirstLap)
                return a.hasStartedFirstLap > b.hasStartedFirstLap;

            if (useFinishOrder)
            {
                const auto aIt = m_finishPositions.find(a.vehicleID);
                const auto bIt = m_finishPositions.find(b.vehicleID);
                const bool aFinished = (aIt != m_finishPositions.end());
                const bool bFinished = (bIt != m_finishPositions.end());

                if (aFinished != bFinished)
                    return aFinished > bFinished;

                if (aFinished && bFinished)
                    return aIt->second < bIt->second;
            }

            if (a.completedLaps != b.completedLaps)
                return a.completedLaps > b.completedLaps;

            return a.distanceFromStart > b.distanceFromStart;
        }
    );
    
    // ========================================================================
    // ASSIGN POSITIONS & DETECT LAPPED CARS
    // ========================================================================
    // isLapped уже проставлен из Vehicle::m_is_lapped выше — здесь только места.
    for (size_t i = 0; i < standings.size(); ++i)
        standings[i].position = static_cast<int>(i + 1);
    
    return standings;
}

// ============================================================================
// GET STANDINGS (sorted leaderboard) - ????????? ?????? ? ???????????
// ?????????? ????? (?? UI, SaveResults ? ?.?.)
// ============================================================================
std::vector<VehicleStanding> RaceManager::GetStandings() const
{
    // Таблица берётся из опубликованного снимка: она посчитана там же, где и
    // всё остальное состояние, и согласована с позициями машин. Пересчитывать
    // её здесь заново означало бы снова читать рабочее состояние пайплайна.
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (!snapshot->standings.empty())
        return snapshot->standings;

    // Снимка ещё нет (первые кадры до первого Update) — считаем напрямую.

    // Таблицу спрашивают 4-7 раз за кадр: главный экран, статус-бар и почти
    // каждая PRO-панель. Каждый пересчёт брал мьютекс машин, считал дельты по
    // всем участникам и сортировал — то есть одно и то же несколько раз подряд
    // с одинаковым результатом. Отдаём снимок, пересчитывая не чаще раза за
    // кадр; данные при этом свежее одного кадра быть всё равно не могут.
    {
        std::lock_guard<std::mutex> cache_lock(m_standings_cache_mutex);

        // Отдельной «заморозки» на время отката повтора здесь нет: откат
        // удерживает мьютекс машин целиком, поэтому пересчёт либо подождёт,
        // либо возьмёт готовое состояние. Удержание старого результата, наоборот,
        // отдавало бы таблицу, отставшую на шаг перемотки.
        if (!m_standings_cache.empty() &&
            (std::chrono::steady_clock::now() - m_standings_cache_time) < STANDINGS_CACHE_TTL)
        {
            return m_standings_cache;
        }
    }

    std::vector<VehicleStanding> standings;
    {
        VehiclesLock lock;
        standings = GetStandingsInternal();
    }

    {
        std::lock_guard<std::mutex> cache_lock(m_standings_cache_mutex);
        m_standings_cache = standings;
        m_standings_cache_time = std::chrono::steady_clock::now();
    }
    return standings;
}

void RaceManager::InvalidateStandingsCache()
{
    std::lock_guard<std::mutex> cache_lock(m_standings_cache_mutex);
    m_standings_cache.clear();
}

// ============================================================================
// CALCULATE DISTANCE FROM START (0.0 to 1.0)
// ? ?????: ?????????? ?????? ???????? ?? ????????? ?????? ?????? ????????? ?????
// ??? ?????? ???????? "???????" ????????? ?? S-???????? ???????? ?????
// ============================================================================
double RaceManager::CalculateDistanceFromStart(const Vehicle& vehicle) const
{
    // ? ?????????? ??????????? ???????? ?? ????????? (0.0-1.0)
    // ??? ?????? ????????? ????? ?????, ?? ??????? ?? ?????????????? ???????? ?????
    return vehicle.m_track_progress;
}

// ============================================================================
// GET LEADER LAP COUNT
// ============================================================================
int RaceManager::GetLeaderLapCount() const
{
    int maxLaps = 0;
    
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        if (vehicle.m_completed_laps > maxLaps)
            maxLaps = vehicle.m_completed_laps;
    }
    
    return maxLaps;
}

// ============================================================================
// LAP DATA ACCESS (thread-safe, reads from Vehicle under mutex)
// ============================================================================
const std::map<int, LapData>* RaceManager::GetVehicleLaps(int32_t vehicleID) const
{
    VehiclesLock lock;
    auto it = g_vehicles.find(vehicleID);
    if (it != g_vehicles.end())
        return &(it->second.m_laps);
    
    return nullptr;
}

std::map<int, LapData> RaceManager::GetVehicleLapsCopy(int32_t vehicleID) const
{
    // Из снимка, как и остальные геттеры для интерфейса: список кругов обязан
    // совпадать с позицией машины и с таблицей. Пока он читался напрямую, при
    // перемотке повтора панель кругов показывала круги другого момента заезда,
    // а сам вызов вставал на мьютексе, который откат держит целиком.
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (const world::VehicleView* view = world::find(*snapshot, vehicleID))
        return view->laps;

    return {};
}

float RaceManager::GetVehicleCurrentLapTime(int32_t vehicleID) const
{
    // Из снимка: таймер обязан быть согласован с позицией машины и с таблицей.
    // Когда он читался напрямую, после перемотки назад показывались новые
    // позиции со старым временем, и цифры прыгали через кадр.
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (const world::VehicleView* view = world::find(*snapshot, vehicleID))
        return view->is_finished ? 0.0f : view->current_lap_timer;

    return 0.0f;
}

int RaceManager::GetVehicleCompletedLaps(int32_t vehicleID) const
{
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (const world::VehicleView* view = world::find(*snapshot, vehicleID))
        return view->completed_laps;

    return 0;
}

float RaceManager::GetVehicleBestLapTime(int32_t vehicleID) const
{
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (const world::VehicleView* view = world::find(*snapshot, vehicleID))
    {
        if (view->completed_laps < RaceConstants::MIN_LAPS_FOR_BEST_LAP)
            return -1.0f;

        return view->best_lap_time;
    }

    return -1.0f;
}

float RaceManager::GetVehiclePreviousLapTime(int32_t vehicleID) const
{
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (const world::VehicleView* view = world::find(*snapshot, vehicleID))
    {
        const int previousLapNumber = view->current_lap_number - 1;
        if (previousLapNumber < RaceConstants::LAP_START_NUMBER)
            return -1.0f;

        const auto lapIt = view->laps.find(previousLapNumber);
        if (lapIt != view->laps.end())
            return lapIt->second.lapTime;
    }

    return -1.0f;
}

// ============================================================================
// TIME DIFFERENCE CALCULATIONS (delegates to TimeDiff functions)
// ============================================================================
float RaceManager::GetVehicleLapDelta(int32_t vehicleID) const
{
    if (m_sessionState == SessionState::Ended)
        return 0.0f;

    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        const world::VehicleView* view = world::find(*snapshot, vehicleID);
        if (view != nullptr && view->is_finished)
            return 0.0f;
    }

    return CalculateLapTimeDiff(vehicleID); // Thread-safe (has mutex inside)
}

float RaceManager::GetVehicleLeaderDelta(int32_t vehicleID) const
{
    if (m_sessionState == SessionState::Ended)
        return 0.0f;

    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        const world::VehicleView* view = world::find(*snapshot, vehicleID);
        if (view != nullptr && view->is_finished)
            return 0.0f;
    }

    return CalculateLeaderTimeDiff(vehicleID); // Thread-safe (has mutex inside)
}

int RaceManager::GetVehicleCurrentLapNumber(int32_t vehicleID) const
{
    const std::shared_ptr<const world::Snapshot> snapshot = world::current();
    if (const world::VehicleView* view = world::find(*snapshot, vehicleID))
        return view->current_lap_number;

    return 0;
}

// ============================================================================
// DEBUG: PRINT SESSION SUMMARY
// ============================================================================
void RaceManager::PrintSessionSummary() const
{
    VehiclesLock lock;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "       RACE SESSION SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& [vehicleID, vehicle] : g_vehicles)
    {
        std::cout << "\nVehicle #" << vehicleID << ":" << std::endl;
        std::cout << "  Completed Laps: " << vehicle.m_completed_laps << std::endl;
        std::cout << "  Current Lap Time: " << vehicle.m_current_lap_timer << "s" << std::endl;
        std::cout << "  Best Lap: " << vehicle.m_best_lap_time << "s" << std::endl;
        
        std::cout << "  Lap Times:" << std::endl;
        for (auto it = vehicle.m_laps.begin(); it != vehicle.m_laps.end(); ++it)
        {
            std::cout << "    Lap " << it->first << ": " << it->second.lapTime << "s" << std::endl;
        }
    }
    
    std::cout << "\n========================================\n" << std::endl;
}

// ============================================================================
// RESULTS REPORT — one text generator for Ctrl+S (save as .txt), Ctrl+P
// (print) and the timestamped auto-save. Works for local (prototype/COM)
// sessions and Track Server sessions alike: standings already come out in
// server classification order when connected.
// ============================================================================
std::string RaceManager::BuildResultsText() const
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    std::ostringstream file;

    file << "========================================\n";
    file << "       RACE SESSION RESULTS\n";
    file << "========================================\n";
    file << "Date: " << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << "\n";
    file << "========================================\n\n";
    
    // ???????? standings ??? ??????????
    std::vector<VehicleStanding> standings = GetStandings();
    
    if (standings.empty())
    {
        file << "No vehicles participated in this session.\n";
    }
    else
    {
        file << "FINAL STANDINGS:\n";
        file << "----------------\n\n";
        
        for (const auto& standing : standings)
        {
            file << "Position: " << standing.position;
            if (standing.isLapped) file << " (LAPPED)";
            file << "\n";
            file << "  Vehicle ID: #" << standing.vehicleID << "\n";
            file << "  Completed Laps: " << standing.completedLaps << "\n";
            
            // Progress (distance from start: 0.000 to 0.999)
            file << "  Progress: " << std::fixed << std::setprecision(3) 
                 << standing.distanceFromStart << "\n";
            
            // ====================================================================
            // TIME DIFFERENCES
            // ====================================================================
            if (standing.deltaTimeToBest != 0.0f && standing.bestLapTime > 0.0f)
            {
                file << "  Delta to Best: ";
                if (standing.deltaTimeToBest > 0)
                    file << "+" << std::fixed << std::setprecision(3) << standing.deltaTimeToBest << "s\n";
                else
                    file << std::fixed << std::setprecision(3) << standing.deltaTimeToBest << "s\n";
            }
            
            if (!standing.isLapped && standing.deltaTimeToLeader != 0.0f)
            {
                file << "  Delta to Leader: ";
                if (standing.deltaTimeToLeader > 0)
                    file << "+" << std::fixed << std::setprecision(3) << standing.deltaTimeToLeader << "s\n";
                else
                    file << std::fixed << std::setprecision(3) << standing.deltaTimeToLeader << "s\n";
            }
            
            // Total race time (sum of all laps + current lap)
            if (standing.completedLaps > 0 || standing.currentLapTime > 0.0f)
            {
                file << "  Total Race Time: " << formatTime(standing.totalRaceTime) << "\n";
            }
            
            if (standing.bestLapTime < 999999.0f)
            {
                file << "  Best Lap Time: " << formatTime(standing.bestLapTime) << "\n";
            }
            else
            {
                file << "  Best Lap Time: N/A\n";
            }
            
            // Get vehicle data for detailed lap info
            VehiclesLock lock;
            auto veh_it = g_vehicles.find(standing.vehicleID);
            if (veh_it != g_vehicles.end())
            {
                const Vehicle& vehicle = veh_it->second;
                
                // Current lap (in progress)
                if (vehicle.m_current_lap_timer > 0.0f)
                {
                    int current_lap_num = vehicle.m_current_lap_number;
                    file << "  Current Lap " << current_lap_num << ": "
                         << formatTime(vehicle.m_current_lap_timer) << " (in progress)\n";
                }
                
                // Completed laps
                if (!vehicle.m_laps.empty())
                {
                    file << "  Lap Times:\n";
                    for (auto lap_it = vehicle.m_laps.begin(); lap_it != vehicle.m_laps.end(); ++lap_it)
                    {
                        file << "    Lap " << lap_it->first << ": "
                             << formatTime(lap_it->second.lapTime) << "\n";
                    }
                }
            }
            
            file << "\n";
        }
    }
    
    file << "========================================\n";
    file << "End of Report\n";
    file << "========================================\n";

    return file.str();
}

// ============================================================================
// SAVE RESULTS TO FILE (timestamped, saves/results)
// ============================================================================
bool RaceManager::SaveResultsToFile() const
{
    std::filesystem::create_directories(app_paths::results());

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    char filename[128];
    std::snprintf(filename, sizeof(filename),
                 "VehicleResults_%04d-%02d-%02d_%02d-%02d-%02d.txt",
                 tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                 tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    const std::filesystem::path path = app_paths::results() / filename;

    std::ofstream file(path);
    if (!file.is_open())
    {
        std::cerr << "[RACE MANAGER] Failed to create file: " << path.string() << std::endl;
        return false;
    }
    file << BuildResultsText();
    file.close();

    std::cout << "[RACE MANAGER] Results saved to: " << path.string() << std::endl;
    return true;
}
