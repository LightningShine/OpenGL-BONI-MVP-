#include "ReplayPlayer.h"

#include "ESP32_Code.h"
#include "../input/Input.h"   // loaded_track_name
#include "SimulationServer.h"
#include "SyntheticTelemetry.h"
#include "TelemetryIngest.h"
#include "../logging/TelemetryLog.h"
#include "../racing/RaceManager.h"
#include "../vehicle/Vehicle.h"
#include "../vehicle/VehicleInterpolator.h"

#include "../Config.h"
#include "../core/AppPaths.h"

#include <rajagp/RajaParser.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <vector>
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
    // производную от хода заезда историю (журнал событий PRO), понимают, что
    // накопленное относится к ещё не проигранной части записи.
    std::atomic<uint64_t> g_rewind_revision{ 0 };

    std::atomic<bool> g_rebuilding{ false };
    std::string g_file_name;
    std::string g_track_name;

    // Причина последнего отказа. Живёт под своим мьютексом: её пишет тот, кто
    // открывает запись, а читает поток отрисовки, когда рисует сообщение.
    std::mutex g_error_mutex;
    std::string g_last_error;

    /// Запоминает причину отказа и сразу дублирует её в консоль. Одна точка на
    /// оба вывода: иначе экран и журнал рано или поздно разойдутся, а разбирают
    /// потом именно журнал.
    bool fail(std::string reason)
    {
        std::cerr << "[REPLAY] " << reason << std::endl;

        std::lock_guard<std::mutex> lock(g_error_mutex);
        g_last_error = std::move(reason);
        return false;
    }

    void clear_error()
    {
        std::lock_guard<std::mutex> lock(g_error_mutex);
        g_last_error.clear();
    }

    // Запросы перемотки НАКАПЛИВАЮТСЯ, а не перезаписываются: при удержании
    // стрелки их приходит по одному на кадр, и каждый несёт свой сдвиг. Копим
    // сумму и применяем одним движением в конце кадра — ровно один шаг на кадр.
    std::atomic<int64_t> g_seek_delta_ms{ 0 };

    // Признак «этот поток сейчас подаёт пакеты записи». Именно потоковый, а не
    // общий: подача идёт из потока проигрывателя, а перемотка — из потока
    // отрисовки, и общий флаг открыл бы окно, в которое проскочил бы чужой
    // пакет из третьего потока.
    thread_local bool t_feeding = false;

    /// Поднимает признак подачи на время своей жизни.
    struct FeedingScope
    {
        FeedingScope() { t_feeding = true; }
        ~FeedingScope() { t_feeding = false; }
    };

    // Как загрузить трассу. Ставится приложением, см. replay_set_track_loader.
    std::function<bool(const std::filesystem::path&)> g_track_loader;

    /// Кладёт встроенную трассу в каталог трасс и возвращает путь к файлу.
    ///
    /// Записываем на диск, а не разбираем из памяти, намеренно: трасса из
    /// записи после этого ничем не отличается от своей, её видно в списке и
    /// можно открыть отдельно. Заодно привезённая на карте памяти гонка
    /// пополняет каталог трасс сама.
    std::filesystem::path materialize_embedded_track(const logging::TelemetryLogReader& reader)
    {
        namespace fs = std::filesystem;

        std::string name = reader.embedded_track_file_name();
        if (name.empty())
        {
            name = (reader.embedded_track_format() == logging::TrackFormat::DualEdgeTrk2)
                       ? "recording_track.trk2" : "recording_track.txt";
        }

        const std::vector<uint8_t>& bytes = reader.embedded_track_bytes();
        fs::path target = app_paths::tracks() / name;

        // Файл с таким именем уже есть. Совпадает побайтово — берём его; не
        // совпадает — кладём рядом под своим именем, чтобы не затереть чужую
        // трассу молча.
        std::error_code error;
        if (fs::exists(target, error))
        {
            std::ifstream existing(target, std::ios::in | std::ios::binary);
            const std::vector<uint8_t> current((std::istreambuf_iterator<char>(existing)),
                                                std::istreambuf_iterator<char>());
            if (current == bytes)
                return target;

            target = app_paths::tracks() /
                     (fs::path(name).stem().string() + " (from recording)" +
                      fs::path(name).extension().string());
        }

        std::ofstream out(target, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            std::cerr << "[REPLAY] Cannot write embedded track to " << target.string() << std::endl;
            return {};
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();

        std::cout << "[REPLAY] Embedded track saved as " << target.string() << std::endl;
        return target;
    }

    std::string to_lower(std::string text)
    {
        for (char& symbol : text)
            symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
        return text;
    }

    /// Одна и та же это трасса или разные.
    ///
    /// Имя в заголовке записи лежит в поле фиксированной длины
    /// (TelemetryLogHeader::track_name), поэтому длинное имя в нём обрезано:
    /// сравнение по равенству объявило бы такую запись сделанной на чужой
    /// трассе. Регистр не различаем — имя пришло из имени файла, а Windows
    /// регистр в именах не различает.
    bool track_names_match(const std::string& recorded, const std::string& loaded)
    {
        constexpr size_t NAME_CAPACITY = sizeof(logging::TelemetryLogHeader::track_name) - 1;

        const std::string left = to_lower(recorded);
        const std::string right = to_lower(loaded);

        if (left == right)
            return true;
        return left.size() == NAME_CAPACITY && right.rfind(left, 0) == 0;
    }

    /// Файл трассы с таким именем в каталоге трасс. Пусто, если её там нет.
    ///
    /// Нужен, чтобы записи, названные трассой, открывались одним действием:
    /// имя трассы в заголовке есть, сама трасса лежит в каталоге — требовать
    /// от оператора открыть её руками не за чем.
    std::filesystem::path find_track_by_name(const std::string& name)
    {
        namespace fs = std::filesystem;
        if (name.empty())
            return {};

        std::error_code error;
        fs::path fallback;   // .txt берём, только если .trk2 не нашлось

        for (const fs::directory_entry& entry : fs::directory_iterator(app_paths::tracks(), error))
        {
            if (!entry.is_regular_file())
                continue;

            const std::string extension = to_lower(entry.path().extension().string());
            if (extension != ".trk2" && extension != ".txt")
                continue;
            if (!track_names_match(name, entry.path().stem().string()))
                continue;

            if (extension == ".trk2")
                return entry.path();
            if (fallback.empty())
                fallback = entry.path();
        }
        return fallback;
    }

    /// Про эту ли трассу запись: где её первый пакет относительно ТРАССЫ.
    ///
    /// Раньше расстояние мерилось от начала координат карты с запасом в
    /// десятки километров — и трасса, отстоящая от места записи на 10 км,
    /// проходила проверку. Опора должна быть та же, по которой приём отличает
    /// свою машину от чужой: расстояние до полотна трассы (см.
    /// TrackConstants::FOREIGN_VEHICLE_RADIUS_METERS). Один критерий на оба
    /// случая — иначе они снова разойдутся.
    bool packets_belong_to_loaded_track(const logging::TelemetryLogReader& reader)
    {
        const uint8_t* first = reader.record(0);
        if (first == nullptr)
            return true;   // пустой записи верим, ломаться не на чем

        rajagp::TelemetryPacket packet{};
        if (!rajagp::parseRajaPayload(first + sizeof(uint32_t), packet))
            return true;

        double normalized_x = 0.0;
        double normalized_y = 0.0;
        normalizedFromGps(packet.lat / 1e7, packet.lon / 1e7, normalized_x, normalized_y);

        return positionIsNearLoadedTrack(normalized_x, normalized_y,
                                         TrackConstants::FOREIGN_VEHICLE_RADIUS_METERS);
    }

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
        const FeedingScope feeding;

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

                const FeedingScope feeding;
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

void replay_set_track_loader(std::function<bool(const std::filesystem::path&)> loader)
{
    g_track_loader = std::move(loader);
}

bool replay_open(const std::filesystem::path& path)
{
    if (g_active.load())
        replay_close();

    clear_error();

    // Живой приём, генератор и повтор кормят ОДИН пайплайн. Запустить два
    // источника разом — значит смешать два потока пакетов в одних машинах:
    // позиции начнут прыгать между заездами, а круги считаться по мешанине.
    // Плюс повтор писался бы в открытый живой журнал — запись записи.
    if (synthetic_is_running())
        return fail("Cannot open the replay: the demo generator is running. Stop it first.");

    if (isRealDataCaptureRunning())
        return fail("Cannot open the replay: live capture is running. Disconnect the receiver first.");

    auto reader = std::make_unique<logging::TelemetryLogReader>(path);
    if (!reader->is_open())
    {
        return fail("Cannot read '" + path.filename().string() +
                    "': the file is damaged or is not a telemetry recording.");
    }

    // ------------------------------------------------------------------------
    // ТРАССА ЗАПИСИ
    //
    // Открыть запись не на той трассе — худший из возможных исходов: позиции,
    // круги и секторы посчитаются, будут выглядеть правдоподобно и не будут
    // значить ничего. Поэтому здесь либо трасса известна точно, либо отказ.
    // ------------------------------------------------------------------------
    const std::string recorded = reader->header().track_name;

    if (reader->has_embedded_track())
    {
        // Запись несёт свою трассу — просто заменяем ею загруженную. Спрашивать
        // не о чем: это ровно та геометрия, на которой заезд и происходил.
        const std::filesystem::path track = materialize_embedded_track(*reader);
        if (track.empty())
            return fail("Cannot save the track stored in the recording to the tracks folder.");

        if (!g_track_loader)
            return fail("Internal error: no track loader is set, the replay cannot open its track.");

        if (!g_track_loader(track))
            return fail("Failed to load the track stored in the recording: " + track.filename().string());

        std::cout << "[REPLAY] Loaded track from recording: " << track.filename().string() << std::endl;
    }
    else
    {
        // Своей трассы у записи нет — так пишет трекер на карту памяти и так
        // выглядят все старые записи. Но её ИМЯ в заголовке обычно есть, а сама
        // трасса — в каталоге трасс: открываем её сами. Требовать открыть её
        // заранее было бы требованием угадать, какая из них нужна: имени
        // трассы в списке записей не видно, а ошибка вылезала бы уже после
        // выбора файла.
        std::string loaded = g_is_map_loaded ? loaded_track_name() : std::string();

        bool track_file_exists = false;
        if (!recorded.empty() && !track_names_match(recorded, loaded))
        {
            const std::filesystem::path track = find_track_by_name(recorded);
            track_file_exists = !track.empty();

            if (track_file_exists && g_track_loader && g_track_loader(track))
            {
                loaded = loaded_track_name();
                std::cout << "[REPLAY] Loaded track of the recording: "
                          << track.filename().string() << std::endl;
            }
        }

        if (!g_is_map_loaded)
        {
            if (recorded.empty())
            {
                return fail("This recording has no track inside and does not name one. "
                            "Open the matching track first, then open the replay.");
            }
            if (track_file_exists)
                return fail("Failed to load track '" + recorded + "' the recording was made on.");

            return fail("This recording has no track inside and track '" + recorded +
                        "' is not in the tracks folder. Open that track first.");
        }

        if (!recorded.empty() && !track_names_match(recorded, loaded))
        {
            return fail("The recording was made on track '" + recorded + "', but '" + loaded +
                        "' is loaded. Open track '" + recorded + "' and try again.");
        }

        if (recorded.empty() && !packets_belong_to_loaded_track(*reader))
        {
            return fail("The recording was made far away from track '" + loaded +
                        "' - it is a different venue. Open the track it was recorded on.");
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_reader = std::move(reader);
        g_file_name = path.filename().string();
        // Трассу берём уже загруженную: у встроенной трассы имя в заголовке
        // может отсутствовать, а показать нужно ту, на которой повтор идёт.
        g_track_name = recorded.empty() ? loaded_track_name() : recorded;
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

    std::cout << "[REPLAY] Opened " << g_file_name << " on track '" << g_track_name << "': "
              << g_reader->record_count() << " records, "
              << (g_reader->duration_ms() / 1000.0) << "s (paused)" << std::endl;
    return true;
}

void replay_close()
{
    const bool was_active = g_active.load();

    g_stop_requested.store(true);
    if (g_thread.joinable())
        g_thread.join();

    g_active.store(false);
    g_position_smoothing_enabled.store(true);   // живой заезд снова сглаживается
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_reader.reset();
        g_file_name.clear();
        g_track_name.clear();
        g_keyframes.clear();
        g_keyframe_interval_ms = KEYFRAME_BASE_INTERVAL_MS;
    }

    if (was_active)
    {
        // Убираем за собой ВСЁ, что оставил после себя просмотренный заезд:
        // машины, круги, таблицу, привязки устройств к номерам. Трасса
        // остаётся — её оператор загружал сам, и она нужна дальше.
        //
        // Иначе закрытие повтора оставляло бы на экране машины из записи, а
        // первый же пакет живого заезда дописывался бы к чужим кругам: номера
        // уже заняты, лучший круг чужой, таблица чужая. Отличить потом одно от
        // другого нельзя.
        {
            VehiclesLock lock;
            g_vehicles.clear();
        }
        telemetryResetAllVehicleState();

        // ResetSession заодно публикует пустой снимок — панели читают только
        // его, и без публикации они держали бы последний кадр записи.
        if (g_race_manager)
            g_race_manager->ResetSession();
    }

    std::cout << "[REPLAY] Closed" << std::endl;
}

bool replay_is_active()
{
    return g_active.load();
}

std::string replay_last_error()
{
    std::lock_guard<std::mutex> lock(g_error_mutex);
    return g_last_error;
}

bool replay_is_feeding()
{
    return t_feeding;
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
    status.track_name = g_track_name;
    status.date_yyyymmdd = g_reader->header().utc_date;
    return status;
}

}
