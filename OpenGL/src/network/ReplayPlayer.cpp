#include "ReplayPlayer.h"

#include "ESP32_Code.h"
#include "SimulationServer.h"
#include "SyntheticTelemetry.h"
#include "TelemetryIngest.h"
#include "../logging/TelemetryLog.h"
#include "../racing/RaceManager.h"
#include "../vehicle/Vehicle.h"
#include "../vehicle/VehicleInterpolator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;
extern std::atomic<bool> g_is_map_loaded;
extern RaceManager* g_race_manager;

namespace telemetry
{
namespace
{
    // Пока проигрыватель на паузе, поток спит короткими интервалами: реакция на
    // снятие паузы должна быть в пределах кадра.
    constexpr std::chrono::milliseconds IDLE_SLEEP{ 5 };

    std::mutex g_mutex;
    std::unique_ptr<logging::TelemetryLogReader> g_reader;
    std::thread g_thread;

    std::atomic<bool> g_active{ false };
    std::atomic<bool> g_stop_requested{ false };
    std::atomic<bool> g_paused{ true };
    std::atomic<double> g_speed{ 1.0 };

    // Перемотка исполняется В КАДРЕ вызывающего потока (см.
    // replay_apply_pending_seek), а поток проигрывателя занят только подачей
    // пакетов в темпе записи. Этот мьютекс разделяет их: подача одного пакета и
    // исполнение перемотки не пересекаются.
    //
    // Порядок захвата: g_seek_mutex → мьютекс машин → g_mutex.
    std::mutex g_seek_mutex;

    std::atomic<size_t> g_position{ 0 };

    // Растёт на каждой перемотке. По нему поток проигрывателя узнаёт, что
    // позиция переставлена под ним, и заново привязывает темп к часам.
    std::atomic<uint64_t> g_seek_generation{ 0 };

    // Растёт на каждом откате назад. По нему потребители, которые НАКАПЛИВАЮТ
    // производную от хода заезда историю (журнал событий PRO, обрезка сэмплов),
    // понимают, что их накопленное относится к ещё не проигранной части записи.
    std::atomic<uint64_t> g_rewind_revision{ 0 };

    std::atomic<bool> g_rebuilding{ false };
    std::string g_file_name;

    // Запросы перемотки НАКАПЛИВАЮТСЯ, а не перезаписываются: при удержании
    // стрелки их приходит по одному на кадр, и каждый несёт свой сдвиг. Копим
    // сумму и применяем одним движением в конце кадра — ровно один шаг на кадр.
    std::atomic<int64_t> g_seek_delta_ms{ 0 };

    // ------------------------------------------------------------------------
    // КЛЮЧЕВЫЕ КАДРЫ
    //
    // Без них перемотка назад стоит прогона записи С НАЧАЛА: на десятиминутной
    // записи с полным полем машин это сотни тысяч пакетов на каждый прыжок.
    // Снимок состояния снимается по ходу воспроизведения, и перемотка в любую
    // точку доигрывает лишь остаток от ближайшего предыдущего снимка.
    //
    // Количество снимков фиксировано, а интервал между ними считается от
    // длины записи. Поэтому стоимость перемотки не зависит от длины файла:
    // всегда не больше одного интервала, а память ограничена сверху.
    // ------------------------------------------------------------------------
    // Снимок берётся каждые 250 мс ЗАПИСИ. Столь частым он может быть потому,
    // что не тащит историю телеметрических сэмплов — она тяжёлая, а для
    // восстановления позиций и кругов не нужна. Чем чаще снимки, тем короче
    // прогон при откате: четверть секунды записи это десятки пакетов, доли
    // миллисекунды работы.
    constexpr uint32_t KEYFRAME_BASE_INTERVAL_MS = 250;

    // Потолок на память. Когда снимков набирается столько, половина
    // выбрасывается, а шаг удваивается: откат становится вдвое дороже, но
    // никогда не скатывается к прогону записи с начала — а это единственное,
    // что даёт заморозки в секунды, а не в миллисекунды.
    constexpr size_t MAX_KEYFRAMES = 4000;

    struct Keyframe
    {
        size_t index = 0;                        // позиция в записи
        std::map<int32_t, Vehicle> vehicles;     // без истории сэмплов, см. capture
    };

    std::vector<Keyframe> g_keyframes;   // по возрастанию index, доступ под g_mutex
    uint32_t g_keyframe_interval_ms = KEYFRAME_BASE_INTERVAL_MS;

    /// Сбрасывает всё, что накопил пайплайн, чтобы прогнать запись заново.
    /// Сессию перезапускаем, если она шла: иначе после перемотки назад круги
    /// перестали бы считаться.
    void reset_pipeline_state()
    {
        const bool was_running = g_race_manager &&
                                 g_race_manager->GetSessionState() != SessionState::Idle;

        {
            VehiclesLock lock;
            g_vehicles.clear();
        }
        // Снимаем ВСЁ состояние по машинам, а не только машины: тайм-синк
        // интерполятора переживал сброс, и пересозданные машины наследовали
        // временные поправки прошлого прогона.
        telemetryResetAllVehicleState();

        if (g_race_manager)
        {
            g_race_manager->ResetSession();
            if (was_running)
                g_race_manager->StartSession();
        }
    }

    /// Оставляет каждый второй снимок и удваивает шаг. Вызывается под g_mutex,
    /// когда упёрлись в потолок по памяти.
    void thin_keyframes()
    {
        std::vector<Keyframe> kept;
        kept.reserve(g_keyframes.size() / 2 + 1);
        for (size_t i = 0; i < g_keyframes.size(); i += 2)
            kept.push_back(std::move(g_keyframes[i]));

        g_keyframes = std::move(kept);
        g_keyframe_interval_ms *= 2;

        std::cout << "[REPLAY] Keyframe budget reached, interval doubled to "
                  << g_keyframe_interval_ms << " ms" << std::endl;
    }

    /// Снимает состояние машин, если рядом с `index` снимка ещё нет.
    ///
    /// Зовётся на КАЖДОМ проходе записи вперёд — и на воспроизведении, и на
    /// перемотке вперёд. Раньше снимки делались только на воспроизведении,
    /// поэтому откат через промотанный вперёд участок не находил ни одного и
    /// каждый раз прогонял запись с самого начала: это и были заморозки.
    void capture_keyframe_if_due(size_t index)
    {
        // Порядок захвата: мьютекс машин ПЕРВЫМ, см. Vehicle.h.
        VehiclesLock vlock;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_reader || index >= g_reader->record_count())
            return;

        const uint32_t elapsed = g_reader->record_elapsed_ms(index);

        // Место в списке, упорядоченном по index. Проходы идут не только слева
        // направо (откат, потом снова вперёд), поэтому вставляем по месту, а не
        // в конец: порядок — инвариант, на нём стоит поиск снимка.
        const auto pos = std::lower_bound(
            g_keyframes.begin(), g_keyframes.end(), index,
            [](const Keyframe& frame, size_t value) { return frame.index < value; });

        // Соседний снимок ближе шага — этот не нужен.
        if (pos != g_keyframes.begin() &&
            elapsed - g_reader->record_elapsed_ms(std::prev(pos)->index) < g_keyframe_interval_ms)
            return;
        if (pos != g_keyframes.end() &&
            g_reader->record_elapsed_ms(pos->index) - elapsed < g_keyframe_interval_ms)
            return;

        Keyframe frame;
        frame.index = index;
        frame.vehicles = g_vehicles;

        // Историю сэмплов из снимка выбрасываем: это самая тяжёлая часть машины
        // (десять замеров в секунду на круг), а восстанавливать её не нужно —
        // при откате она остаётся у живых машин нетронутой.
        for (auto& [id, vehicle] : frame.vehicles)
        {
            vehicle.laps.clear();
            vehicle.m_pending_crossings.clear();
        }

        g_keyframes.insert(pos, std::move(frame));

        if (g_keyframes.size() >= MAX_KEYFRAMES)
            thin_keyframes();
    }

    /// Подаёт записи [from, to) в общий тракт приёма без выдержки темпа.
    /// `capture` включает съём снимков: он нужен на проходах вперёд, где
    /// состояние после каждого пакета полное, и не нужен внутри отката, где
    /// участок уже покрыт снимком, от которого и доигрывается.
    void feed_range(size_t from, size_t to, bool capture)
    {
        for (size_t i = from; i < to && !g_stop_requested.load(); ++i)
        {
            const uint8_t* record = g_reader->record(i);
            if (record != nullptr)
                ingest_wire_packet(record + sizeof(uint32_t));  // пропускаем маркер

            if (capture)
                capture_keyframe_if_due(i + 1);
        }
    }

    /// Ближайший снимок НЕ ПОЗЖЕ target. nullptr, если такого ещё нет.
    const Keyframe* find_keyframe_before(size_t target)
    {
        const auto pos = std::upper_bound(
            g_keyframes.begin(), g_keyframes.end(), target,
            [](size_t value, const Keyframe& frame) { return value < frame.index; });

        return (pos == g_keyframes.begin()) ? nullptr : &*std::prev(pos);
    }

    /// Восстанавливает машины из снимка. Привязки устройств не трогаем: они
    /// действуют на всю запись, а сброс выдал бы устройству новый номер при
    /// живой машине со старым.
    void restore_keyframe(const Keyframe& frame)
    {
        {
            VehiclesLock lock;

            // Машин, которых в снимке не было, на тот момент ещё не существовало.
            for (auto it = g_vehicles.begin(); it != g_vehicles.end();)
                it = (frame.vehicles.count(it->first) == 0) ? g_vehicles.erase(it) : std::next(it);

            for (const auto& [id, snapshot] : frame.vehicles)
            {
                auto it = g_vehicles.find(id);
                if (it == g_vehicles.end())
                {
                    g_vehicles.emplace(id, snapshot);
                    continue;
                }

                // История сэмплов в снимке не хранится — переносим её с живой
                // машины, иначе графики и дельты обнулялись бы при каждом откате.
                auto samples = std::move(it->second.laps);
                it->second = snapshot;
                it->second.laps = std::move(samples);
            }
        }
        telemetryResetInterpolationState();
    }

    /// Переставляет позицию. Вперёд — досылаем недостающие записи; назад —
    /// откатываемся на ближайший снимок и доигрываем остаток.
    void apply_seek(size_t target)
    {
        const size_t total = g_reader->record_count();
        if (target > total)
            target = total;

        const size_t current = g_position.load();
        if (target >= current)
        {
            // Вперёд — просто досылаем недостающее: состояние накопительное,
            // прогон этих же пакетов даёт ровно то же, что и обычная игра.
            // Попутно снимаем ключевые кадры: назад через этот участок пойдут
            // уже по снимкам, а не прогоном записи с начала.
            feed_range(current, target, /*capture=*/true);

            // Публикуем сами, а не ждём Update: перемотка обязана обновлять
            // экран одинаково в обе стороны и не зависеть от того, дошёл ли до
            // Update главный цикл.
            if (g_race_manager)
            {
                VehiclesLock lock;
                g_race_manager->PublishPositionsOnly();
            }
        }
        else
        {
            // Назад — откат на ближайший снимок и доигрывание остатка. Полный
            // пересчёт с нуля остаётся только если снимков ещё нет.
            // На время отката поднимаем флаг: пока он поднят, гоночная логика
            // не трогается и не считает результат по половине записи.
            g_rebuilding.store(true);
            g_pipeline_rebuilding.store(true);

            // Держим мьютекс машин на ВЕСЬ откат. Любой читатель — карта PRO,
            // таймер круга, панели — обязан взять этот же мьютекс, поэтому
            // подождёт и увидит только «до» и «после». Точечная заморозка
            // отдельных потребителей эту задачу не решала: их больше десятка,
            // и незакрытые как раз и давали прыжки на карте.
            enter_vehicles_bulk_section();

            size_t from = 0;
            bool restored = false;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (const Keyframe* frame = find_keyframe_before(target))
                {
                    restore_keyframe(*frame);
                    from = frame->index;
                    restored = true;
                }
            }

            if (!restored)
                reset_pipeline_state();

            feed_range(from, target, /*capture=*/false);

            // Диагностика: если окно пересчёта вдруг стало длинным, это будет
            // видно в журнале, а не только глазами по прыжкам на карте.
            const size_t replayed = target - from;
            if (replayed > 2000)
            {
                std::cout << "[REPLAY] Long rebuild: " << replayed
                          << " packets (no nearby keyframe)" << std::endl;
            }

            // На шаге публикуем только позиции: полный пересчёт таблицы с
            // дельтами по всем машинам стоит дорого. Точную гоночную логику
            // досчитывает Update главного цикла в следующем кадре — перемотка
            // к тому времени уже отработала свой шаг и мьютекс отпустила.
            if (g_race_manager)
                g_race_manager->PublishPositionsOnly();

            leave_vehicles_bulk_section();

            g_rewind_revision.fetch_add(1);
            g_rebuilding.store(false);
            g_pipeline_rebuilding.store(false);
        }
        g_position.store(target);
        g_seek_generation.fetch_add(1);

        // Сбрасываем сглаживание ПОСЛЕ ЛЮБОЙ перемотки, в том числе вперёд.
        // Интерполятор привязывает метки пакетов к локальным часам один раз, а
        // перемотка эту привязку ломает: время записи прыгает, локальное — нет.
        // Из-за этого машина после отпускания клавиши доезжала до места рывком.
        // Сброс заставляет привязку установиться заново от текущей точки.
        telemetryResetInterpolationState();
    }

    size_t index_for_offset_ms(int64_t offset_ms)
    {
        const size_t total = g_reader->record_count();
        if (total == 0)
            return 0;

        int64_t target_ms = static_cast<int64_t>(
            g_reader->record_elapsed_ms(g_position.load() < total ? g_position.load() : total - 1));
        target_ms += offset_ms;
        if (target_ms < 0)
            target_ms = 0;

        const uint32_t first_utc = g_reader->record_utc_ms(0);
        const uint32_t wanted_utc =
            static_cast<uint32_t>((static_cast<uint64_t>(first_utc) + static_cast<uint64_t>(target_ms))
                                  % 86'400'000ull);
        return g_reader->find_index_at_or_after(wanted_utc);
    }

    void player_loop()
    {
        auto segment_start_wall = std::chrono::steady_clock::now();
        uint32_t segment_start_ms = 0;
        bool timing_valid = false;

        uint64_t seen_seek_generation = g_seek_generation.load();

        while (!g_stop_requested.load())
        {
            // Перемотку исполняет главный поток в кадре, здесь только замечаем
            // её: после перестановки позиции привязка темпа к часам недействительна.
            if (const uint64_t generation = g_seek_generation.load();
                generation != seen_seek_generation)
            {
                seen_seek_generation = generation;
                timing_valid = false;
            }

            if (g_paused.load())
            {
                std::this_thread::sleep_for(IDLE_SLEEP);
                timing_valid = false;
                continue;
            }

            size_t index = 0;
            {
                // Перемотка и подача пакета не пересекаются: иначе перемотка
                // переставила бы позицию между чтением индекса и записью
                // следующего, и один пакет ушёл бы в тракт мимо новой точки.
                std::lock_guard<std::mutex> lock(g_seek_mutex);

                index = g_position.load();
                if (index >= g_reader->record_count())
                {
                    g_paused.store(true);   // доиграли до конца — встаём на паузу
                    g_position_smoothing_enabled.store(false);
                    continue;
                }

                const uint8_t* record = g_reader->record(index);
                if (record != nullptr)
                    ingest_wire_packet(record + sizeof(uint32_t));
                g_position.store(index + 1);

                capture_keyframe_if_due(index + 1);
            }

            // Темп держим по абсолютным дедлайнам от начала отрезка
            // воспроизведения: sleep на фиксированную паузу копит отставание
            // (на Windows разрешение таймера ~15.6 мс).
            if (!timing_valid)
            {
                segment_start_wall = std::chrono::steady_clock::now();
                segment_start_ms = g_reader->record_elapsed_ms(index);
                timing_valid = true;
                continue;
            }

            const double speed = g_speed.load();
            if (speed <= 0.0)
                continue;

            const uint32_t next_ms = g_reader->record_elapsed_ms(index + 1);
            if (next_ms <= segment_start_ms)
                continue;

            const double offset_us = (next_ms - segment_start_ms) * 1000.0 / speed;
            const auto deadline = segment_start_wall +
                                  std::chrono::microseconds(static_cast<long long>(offset_us));
            if (deadline > std::chrono::steady_clock::now())
                std::this_thread::sleep_until(deadline);
        }
    }
}

bool replay_open(const std::filesystem::path& path)
{
    if (g_active.load())
        replay_close();

    if (!g_is_map_loaded)
    {
        std::cerr << "[REPLAY] Cannot start: load the matching track first" << std::endl;
        return false;
    }

    // Живой приём, генератор и повтор кормят ОДИН пайплайн. Запустить два
    // источника разом — значит смешать два потока пакетов в одних машинах:
    // позиции начнут прыгать между заездами, а круги считаться по мешанине.
    // Плюс повтор писался бы в открытый живой журнал — запись записи.
    if (synthetic_is_running())
    {
        std::cerr << "[REPLAY] Cannot start: synthetic generator is running" << std::endl;
        return false;
    }

    if (isRealDataCaptureRunning())
    {
        std::cerr << "[REPLAY] Cannot start: COM capture is running, disconnect it first" << std::endl;
        return false;
    }

    auto reader = std::make_unique<logging::TelemetryLogReader>(path);
    if (!reader->is_open())
        return false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_reader = std::move(reader);
        g_file_name = path.filename().string();
        g_keyframes.clear();
        g_keyframe_interval_ms = KEYFRAME_BASE_INTERVAL_MS;
    }

    // Повтор НЕ открывает журнал: иначе получилась бы запись записи.
    reset_pipeline_state();

    // Запись делалась во время заезда, значит и воспроизводить её надо в
    // состоянии гонки — иначе круги не считались бы вовсе. Моменты старта и
    // стопа в файле пока не хранятся (там только эфир), поэтому сессию
    // запускаем сразу с начала записи.
    if (g_race_manager)
        g_race_manager->StartSession();

    g_position.store(0);
    g_paused.store(true);
    g_position_smoothing_enabled.store(false);
    g_speed.store(1.0);
    g_stop_requested.store(false);
    g_active.store(true);

    if (g_thread.joinable())
        g_thread.join();
    g_thread = std::thread(player_loop);

    std::cout << "[REPLAY] Opened " << g_file_name << ": "
              << g_reader->record_count() << " records, "
              << (g_reader->duration_ms() / 1000.0) << "s (paused)" << std::endl;
    return true;
}

void replay_close()
{
    g_stop_requested.store(true);
    if (g_thread.joinable())
        g_thread.join();

    g_active.store(false);
    g_position_smoothing_enabled.store(true);   // живой заезд снова сглаживается
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_reader.reset();
        g_file_name.clear();
        g_keyframes.clear();
        g_keyframe_interval_ms = KEYFRAME_BASE_INTERVAL_MS;
    }
    std::cout << "[REPLAY] Closed" << std::endl;
}

bool replay_is_active()
{
    return g_active.load();
}

void replay_toggle_pause()
{
    if (!g_active.load())
        return;

    const bool paused = !g_paused.load();
    g_paused.store(paused);

    // Сглаживание позиций работает только при воспроизведении в реальном
    // темпе. На паузе оно опиралось бы на пачку пакетов с чужими метками.
    g_position_smoothing_enabled.store(!paused);
}

bool replay_is_paused()
{
    return g_active.load() && g_paused.load();
}

void replay_set_speed(double speed)
{
    if (speed > 0.0)
        g_speed.store(speed);
}

void replay_step(int ticks)
{
    if (!g_active.load())
        return;

    g_paused.store(true);   // покадровый шаг подразумевает паузу

    g_position_smoothing_enabled.store(false);
    g_seek_delta_ms.fetch_add(static_cast<int64_t>(ticks) * REPLAY_TICK_MS);
}

uint64_t replay_rewind_revision()
{
    return g_rewind_revision.load();
}

void replay_trim_history_if_rewound()
{
    static uint64_t s_trimmed_revision = 0;

    if (!g_active.load())
        return;

    const uint64_t revision = g_rewind_revision.load();
    if (revision == s_trimmed_revision)
        return;
    s_trimmed_revision = revision;

    // Времена кругов (m_laps) снимок хранит и откатывает вместе с машиной, а
    // историю телеметрических сэмплов — нет: она самая тяжёлая часть машины, и
    // тащить её в каждый снимок нельзя. Поэтому после отката она оставалась от
    // ЕЩЁ НЕ ПРОИГРАННОЙ части записи: PRO-панели секторов и карты показывали
    // времена участка, до которого воспроизведение не доехало, рядом с таблицей
    // на десять кругов раньше. Обрезаем хвост — тогда история согласована с
    // временами кругов, а при движении вперёд запишется заново.
    VehiclesLock lock;
    for (auto& [id, vehicle] : g_vehicles)
    {
        // Кругов за текущим на этой точке записи ещё не было.
        vehicle.laps.erase(vehicle.laps.upper_bound(vehicle.m_current_lap_number),
                           vehicle.laps.end());

        const auto lap = vehicle.laps.find(vehicle.m_current_lap_number);
        if (lap == vehicle.laps.end())
            continue;

        // Внутри круга сэмплы идут по возрастанию прогресса, поэтому хвост
        // отсекается двоичным поиском: панели читают эту историю каждый кадр,
        // и перебирать её целиком на каждом шаге перемотки нельзя.
        std::vector<LapInfo>& samples = lap->second.samples;
        const auto cut = std::upper_bound(
            samples.begin(), samples.end(), vehicle.m_track_progress,
            [](double progress, const LapInfo& sample) { return progress < sample.progress; });

        samples.erase(cut, samples.end());
    }
}

void replay_apply_pending_seek()
{
    // Дешёвая проверка без блокировок: зовут каждый кадр, а перемотка идёт
    // редко.
    if (!g_active.load() || g_seek_delta_ms.load() == 0)
        return;

    std::lock_guard<std::mutex> lock(g_seek_mutex);

    const int64_t delta = g_seek_delta_ms.exchange(0);
    if (delta != 0)
        apply_seek(index_for_offset_ms(delta));
}

void replay_scrub(double seconds)
{
    if (!g_active.load() || seconds == 0.0)
        return;

    // Перемотка ставит на паузу. Без этого воспроизведение продолжало тянуть
    // позицию вперёд, пока удержание клавиши тянуло назад: машина прыгала
    // между двумя точками. Возобновление — пробелом, как в любом плеере.
    g_paused.store(true);
    g_position_smoothing_enabled.store(false);
    g_seek_delta_ms.fetch_add(static_cast<int64_t>(seconds * 1000.0));
}

bool replay_is_rebuilding()
{
    return g_rebuilding.load();
}

ReplayStatus replay_status()
{
    ReplayStatus status;
    status.active = g_active.load();
    if (!status.active)
        return status;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_reader)
        return status;

    status.paused = g_paused.load();
    status.speed = g_speed.load();
    status.position = g_position.load();
    status.total = g_reader->record_count();
    status.duration_ms = g_reader->duration_ms();
    status.position_ms = g_reader->record_elapsed_ms(
        status.position < status.total ? status.position : (status.total > 0 ? status.total - 1 : 0));
    status.file_name = g_file_name;
    return status;
}

}
