#include "ReplayPlayer.h"

#include "SimulationServer.h"
#include "SyntheticTelemetry.h"
#include "TelemetryIngest.h"
#include "../logging/TelemetryLog.h"
#include "../racing/RaceManager.h"
#include "../vehicle/Vehicle.h"
#include "../vehicle/VehicleInterpolator.h"

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

    // Позиция и запрошенная перемотка. Перемотку исполняет поток
    // проигрывателя: полный пересчёт занимает заметное время, и делать его в
    // кадре нельзя.
    std::atomic<size_t> g_position{ 0 };
    std::atomic<bool> g_seek_pending{ false };
    std::atomic<int64_t> g_seek_target_ms{ 0 };
    std::string g_file_name;

    /// Сбрасывает всё, что накопил пайплайн, чтобы прогнать запись заново.
    /// Сессию перезапускаем, если она шла: иначе после перемотки назад круги
    /// перестали бы считаться.
    void reset_pipeline_state()
    {
        const bool was_running = g_race_manager &&
                                 g_race_manager->GetSessionState() != SessionState::Idle;

        {
            std::lock_guard<std::mutex> lock(g_vehicles_mutex);
            g_vehicles.clear();
        }
        VehicleInterpolator::Get().Clear();
        telemetryResetPrototypeIdMapping();

        if (g_race_manager)
        {
            g_race_manager->ResetSession();
            if (was_running)
                g_race_manager->StartSession();
        }
    }

    /// Подаёт записи [from, to) в общий тракт приёма без выдержки темпа.
    void feed_range(size_t from, size_t to)
    {
        for (size_t i = from; i < to && !g_stop_requested.load(); ++i)
        {
            const uint8_t* record = g_reader->record(i);
            if (record != nullptr)
                ingest_wire_packet(record + sizeof(uint32_t));  // пропускаем маркер
        }
    }

    /// Переставляет позицию. Вперёд — досылаем недостающие записи; назад —
    /// пересчитываем с начала, потому что состояние гонки накопительное.
    void apply_seek(size_t target)
    {
        const size_t total = g_reader->record_count();
        if (target > total)
            target = total;

        const size_t current = g_position.load();
        if (target >= current)
        {
            feed_range(current, target);
        }
        else
        {
            reset_pipeline_state();
            feed_range(0, target);
        }
        g_position.store(target);
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

        while (!g_stop_requested.load())
        {
            if (g_seek_pending.exchange(false))
            {
                apply_seek(index_for_offset_ms(g_seek_target_ms.load()));
                timing_valid = false;
            }

            if (g_paused.load())
            {
                std::this_thread::sleep_for(IDLE_SLEEP);
                timing_valid = false;
                continue;
            }

            const size_t index = g_position.load();
            if (index >= g_reader->record_count())
            {
                g_paused.store(true);   // доиграли до конца — встаём на паузу
                continue;
            }

            const uint8_t* record = g_reader->record(index);
            if (record != nullptr)
                ingest_wire_packet(record + sizeof(uint32_t));
            g_position.store(index + 1);

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

    if (synthetic_is_running())
    {
        std::cerr << "[REPLAY] Cannot start: synthetic generator is running" << std::endl;
        return false;
    }

    auto reader = std::make_unique<logging::TelemetryLogReader>(path);
    if (!reader->is_open())
        return false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_reader = std::move(reader);
        g_file_name = path.filename().string();
    }

    // Повтор НЕ открывает журнал: иначе получилась бы запись записи.
    reset_pipeline_state();

    g_position.store(0);
    g_paused.store(true);
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
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_reader.reset();
        g_file_name.clear();
    }
    std::cout << "[REPLAY] Closed" << std::endl;
}

bool replay_is_active()
{
    return g_active.load();
}

void replay_toggle_pause()
{
    if (g_active.load())
        g_paused.store(!g_paused.load());
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
    g_seek_target_ms.store(static_cast<int64_t>(ticks) * REPLAY_TICK_MS);
    g_seek_pending.store(true);
}

void replay_scrub(double seconds)
{
    if (!g_active.load() || seconds == 0.0)
        return;

    g_seek_target_ms.store(static_cast<int64_t>(seconds * 1000.0));
    g_seek_pending.store(true);
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
