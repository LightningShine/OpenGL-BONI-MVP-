#include "TelemetryLog.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <rajagp/RajaParser.h>

// Арифметика времени с учётом полуночи — одна реализация на хронометраж и на
// чтение записей, чтобы повтор считал время ровно так же, как живой заезд.
#include "../racing/LapClock.h"

namespace logging
{
namespace
{
    constexpr size_t RECORD_SIZE  = sizeof(rajagp::RajaTelemetryPacket);          // 37
    constexpr size_t PAYLOAD_SIZE = static_cast<size_t>(rajagp::kRajaPayloadAfterMagic); // 33

    // Смещение gps_utc_ms внутри payload (в пакете это +9, payload идёт с +4).
    constexpr size_t PAYLOAD_UTC_MS_OFFSET = 5;

    // Маркер 'RAJA' в том виде, в каком он лежит на проводе (little-endian).
    constexpr uint8_t RAJA_MAGIC_BYTES[4] = { 0x41, 0x4A, 0x41, 0x52 };

    constexpr uint32_t MS_PER_DAY = racing::MS_PER_DAY;

    // Потолок очереди. При 20 машинах на 50 Гц поток равен 37 КБ/с, так что
    // двух мегабайт хватает почти на минуту затыка диска. Дальше записи
    // отбрасываются: журнал не имеет права съесть память приложения.
    constexpr size_t MAX_PENDING_BYTES = 2u * 1024u * 1024u;

    // Как часто писатель просыпается, если очередь пуста.
    constexpr std::chrono::milliseconds WRITER_IDLE_WAIT{ 500 };

    /// Локальная дата как ГГГГММДД — в gps_utc_ms есть время суток, но нет даты.
    uint32_t local_date_yyyymmdd()
    {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &seconds);
        return static_cast<uint32_t>((local.tm_year + 1900) * 10000 +
                                     (local.tm_mon + 1) * 100 +
                                     local.tm_mday);
    }

    /// Имя файла: сортировка по имени совпадает с сортировкой по времени.
    std::string make_log_file_name()
    {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &seconds);

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "telemetry_%04d%02d%02d_%02d%02d%02d.rjl",
                      local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec);
        return buffer;
    }

    uint32_t read_utc_ms(const uint8_t* payload_after_magic)
    {
        uint32_t value = 0;
        std::memcpy(&value, payload_after_magic + PAYLOAD_UTC_MS_OFFSET, sizeof(value));
        return value;
    }
}

// ----------------------------------------------------------------------------
// Проверка файла
// ----------------------------------------------------------------------------
TelemetryLogStats verify_telemetry_log(const std::filesystem::path& path)
{
    TelemetryLogStats stats;

    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open())
        return stats;

    TelemetryLogHeader header{};
    stream.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(header)))
        return stats;

    stats.header_valid = (header.magic == TELEMETRY_LOG_MAGIC) &&
                         (header.record_size == RECORD_SIZE);
    if (!stats.header_valid)
        return stats;

    std::vector<uint8_t> record(RECORD_SIZE);
    rajagp::TelemetryPacket packet{};

    while (stream.read(reinterpret_cast<char*>(record.data()), RECORD_SIZE))
    {
        ++stats.records_total;
        // Проверяем ровно тем же кодом, что и приём с порта: одна реализация
        // формата на запись, чтение и эфир.
        if (std::memcmp(record.data(), RAJA_MAGIC_BYTES, sizeof(RAJA_MAGIC_BYTES)) == 0 &&
            rajagp::parseRajaPayload(record.data() + sizeof(RAJA_MAGIC_BYTES), packet))
        {
            ++stats.records_valid;
        }
    }
    stats.trailing_bytes = static_cast<uint64_t>(stream.gcount());

    return stats;
}

// ----------------------------------------------------------------------------
// TelemetryLogReader
// ----------------------------------------------------------------------------
struct TelemetryLogReader::Impl
{
    TelemetryLogHeader header{};
    bool valid = false;

    // Только целые записи, прошедшие CRC, подряд без дыр.
    std::vector<uint8_t> records;

    // Время каждой записи в миллисекундах ОТ НАЧАЛА записи. Считается отдельно,
    // потому что сырой gps_utc_ms для поиска не годится: он обнуляется в
    // полночь, а пакеты разных машин идут вперемешку с разбросом в пару
    // миллисекунд. Здесь же массив неубывающий — по нему можно искать двоичным
    // поиском.
    std::vector<uint32_t> elapsed_ms;

    size_t count() const { return elapsed_ms.size(); }
};

TelemetryLogReader::TelemetryLogReader(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>())
{
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open())
    {
        std::cerr << "[TELEMETRY-LOG] Cannot open " << path.string() << std::endl;
        return;
    }

    stream.read(reinterpret_cast<char*>(&impl_->header), sizeof(TelemetryLogHeader));
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(TelemetryLogHeader)) ||
        impl_->header.magic != TELEMETRY_LOG_MAGIC ||
        impl_->header.record_size != RECORD_SIZE)
    {
        std::cerr << "[TELEMETRY-LOG] " << path.string() << " is not a valid recording" << std::endl;
        return;
    }

    std::vector<uint8_t> record(RECORD_SIZE);
    rajagp::TelemetryPacket packet{};

    // Разворачиваем сутки: время идёт вперёд, а метка после полуночи начинается
    // с нуля. Порогом отличаем настоящий переход через полночь от обычного
    // разброса меток соседних машин.
    constexpr uint32_t MIDNIGHT_JUMP_THRESHOLD = 12u * 3600u * 1000u;
    uint64_t day_offset = 0;
    uint32_t previous_raw = 0;
    uint64_t base = 0;
    uint32_t previous_elapsed = 0;
    bool first = true;

    while (stream.read(reinterpret_cast<char*>(record.data()), RECORD_SIZE))
    {
        if (std::memcmp(record.data(), RAJA_MAGIC_BYTES, sizeof(RAJA_MAGIC_BYTES)) != 0 ||
            !rajagp::parseRajaPayload(record.data() + sizeof(RAJA_MAGIC_BYTES), packet))
        {
            continue;  // битую запись просто не показываем наружу
        }

        const uint32_t raw = read_utc_ms(record.data() + sizeof(RAJA_MAGIC_BYTES));

        if (first)
        {
            base = raw;
            previous_raw = raw;
            first = false;
        }
        else if (raw + MIDNIGHT_JUMP_THRESHOLD < previous_raw)
        {
            day_offset += MS_PER_DAY;
        }
        previous_raw = raw;

        const uint64_t absolute = static_cast<uint64_t>(raw) + day_offset;
        uint32_t elapsed = (absolute > base) ? static_cast<uint32_t>(absolute - base) : 0;

        // Небольшой разброс между машинами не должен ломать монотонность:
        // массив обязан быть неубывающим, иначе двоичный поиск неприменим.
        if (elapsed < previous_elapsed)
            elapsed = previous_elapsed;
        previous_elapsed = elapsed;

        impl_->records.insert(impl_->records.end(), record.begin(), record.end());
        impl_->elapsed_ms.push_back(elapsed);
    }

    impl_->valid = true;
    std::cout << "[TELEMETRY-LOG] Loaded " << path.filename().string() << ": "
              << impl_->count() << " records, "
              << (duration_ms() / 1000.0) << "s" << std::endl;
}

TelemetryLogReader::~TelemetryLogReader() = default;

bool TelemetryLogReader::is_open() const
{
    return impl_ && impl_->valid && impl_->count() > 0;
}

const TelemetryLogHeader& TelemetryLogReader::header() const
{
    return impl_->header;
}

size_t TelemetryLogReader::record_count() const
{
    return impl_ ? impl_->count() : 0;
}

const uint8_t* TelemetryLogReader::record(size_t index) const
{
    if (!impl_ || index >= impl_->count())
        return nullptr;
    return impl_->records.data() + index * RECORD_SIZE;
}

uint32_t TelemetryLogReader::record_utc_ms(size_t index) const
{
    const uint8_t* bytes = record(index);
    return bytes ? read_utc_ms(bytes + sizeof(RAJA_MAGIC_BYTES)) : 0;
}

uint32_t TelemetryLogReader::record_elapsed_ms(size_t index) const
{
    if (!impl_ || index >= impl_->count())
        return 0;
    return impl_->elapsed_ms[index];
}

size_t TelemetryLogReader::find_index_at_or_after(uint32_t utc_ms) const
{
    if (!impl_ || impl_->count() == 0)
        return 0;

    const uint32_t first_utc = record_utc_ms(0);
    const uint32_t target_elapsed = racing::utc_elapsed_ms(first_utc, utc_ms);

    const auto it = std::lower_bound(impl_->elapsed_ms.begin(), impl_->elapsed_ms.end(),
                                     target_elapsed);
    return static_cast<size_t>(std::distance(impl_->elapsed_ms.begin(), it));
}

uint32_t TelemetryLogReader::duration_ms() const
{
    if (!impl_ || impl_->elapsed_ms.empty())
        return 0;
    return impl_->elapsed_ms.back();
}

// ----------------------------------------------------------------------------
// TelemetryLogWriter
// ----------------------------------------------------------------------------
struct TelemetryLogWriter::Impl
{
    std::filesystem::path path;
    std::ofstream stream;
    TelemetryLogSource source = TelemetryLogSource::Receiver;

    std::mutex mutex;
    std::condition_variable queue_ready;
    std::vector<uint8_t> pending;    // сюда пишет поток приёма
    std::vector<uint8_t> flushing;   // сюда меняется писатель, чтобы не держать лок
    bool stop_requested = false;

    std::thread worker;
    std::atomic<uint64_t> written{ 0 };
    std::atomic<uint64_t> dropped{ 0 };

    uint32_t first_utc_ms = 0;
    uint32_t last_utc_ms = 0;

    void writer_loop();
    void write_header(bool finalize);
};

void TelemetryLogWriter::Impl::write_header(bool finalize)
{
    TelemetryLogHeader header{};
    header.magic = TELEMETRY_LOG_MAGIC;
    header.version = TELEMETRY_LOG_VERSION;
    header.record_size = static_cast<uint16_t>(RECORD_SIZE);
    header.utc_date = local_date_yyyymmdd();
    header.source = static_cast<uint8_t>(source);
    header.track_embedded = 0;
    // TODO: заполнять track_name, когда в приложении появится имя текущего трека
    // (сейчас загруженный трек нигде не хранится под именем).

    if (finalize)
    {
        header.first_utc_ms = first_utc_ms;
        header.last_utc_ms = last_utc_ms;
        header.record_count = static_cast<uint32_t>(written.load(std::memory_order_relaxed));
    }
    // Иначе поля остаются нулями — это штатный признак «запись оборвалась»,
    // читатель в таком случае берёт количество из размера файла.

    stream.seekp(0, std::ios::beg);
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.seekp(0, std::ios::end);
    stream.flush();
}

void TelemetryLogWriter::Impl::writer_loop()
{
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            queue_ready.wait_for(lock, WRITER_IDLE_WAIT,
                                 [this] { return stop_requested || !pending.empty(); });

            const bool finished = stop_requested && pending.empty();
            flushing.swap(pending);
            pending.clear();

            if (finished)
                break;
        }

        if (flushing.empty())
            continue;

        // Диск трогаем уже без лока: поток приёма COM-порта не должен ждать
        // ввод-вывод, иначе переполнится буфер порта и связь «зависнет».
        stream.write(reinterpret_cast<const char*>(flushing.data()),
                     static_cast<std::streamsize>(flushing.size()));
        stream.flush();
        flushing.clear();
    }
}

TelemetryLogWriter::TelemetryLogWriter(const std::filesystem::path& directory,
                                       TelemetryLogSource source)
    : impl_(std::make_unique<Impl>())
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        std::cerr << "[TELEMETRY-LOG] Cannot create directory " << directory.string()
                  << ": " << error.message() << std::endl;
        impl_.reset();
        return;
    }

    impl_->source = source;
    impl_->path = directory / make_log_file_name();
    impl_->stream.open(impl_->path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!impl_->stream.is_open())
    {
        std::cerr << "[TELEMETRY-LOG] Cannot open " << impl_->path.string() << std::endl;
        impl_.reset();
        return;
    }

    impl_->write_header(false);
    impl_->pending.reserve(MAX_PENDING_BYTES / 16);
    impl_->worker = std::thread([impl = impl_.get()] { impl->writer_loop(); });

    std::cout << "[TELEMETRY-LOG] Recording to " << impl_->path.string()
              << " (" << RECORD_SIZE << " bytes per record)" << std::endl;
}

TelemetryLogWriter::~TelemetryLogWriter()
{
    if (!impl_)
        return;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop_requested = true;
    }
    impl_->queue_ready.notify_all();
    if (impl_->worker.joinable())
        impl_->worker.join();

    impl_->write_header(true);
    impl_->stream.close();

    const uint64_t written = impl_->written.load(std::memory_order_relaxed);
    const uint64_t dropped = impl_->dropped.load(std::memory_order_relaxed);
    std::cout << "[TELEMETRY-LOG] Closed " << impl_->path.string()
              << ": " << written << " records";
    if (dropped > 0)
        std::cout << ", " << dropped << " DROPPED (writer could not keep up)";
    std::cout << std::endl;

    // Самопроверка: файл перечитывается тем же парсером, что и эфир. Если тут
    // не сходится, значит запись сломана, и это надо знать сразу, а не через
    // неделю при разборе гонки.
    const TelemetryLogStats stats = verify_telemetry_log(impl_->path);
    if (!stats.header_valid || stats.records_valid != stats.records_total ||
        stats.records_total != written || stats.trailing_bytes != 0)
    {
        std::cerr << "[TELEMETRY-LOG] VERIFY FAILED: header_valid=" << stats.header_valid
                  << " total=" << stats.records_total
                  << " valid=" << stats.records_valid
                  << " expected=" << written
                  << " trailing=" << stats.trailing_bytes << std::endl;
    }
    else
    {
        std::cout << "[TELEMETRY-LOG] Verified " << stats.records_valid
                  << " records, all CRC ok" << std::endl;
    }
}

void TelemetryLogWriter::write(const uint8_t* payload_after_magic)
{
    if (!impl_ || payload_after_magic == nullptr)
        return;

    const uint32_t utc_ms = read_utc_ms(payload_after_magic);

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->stop_requested)
            return;

        if (impl_->pending.size() + RECORD_SIZE > MAX_PENDING_BYTES)
        {
            impl_->dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        impl_->pending.insert(impl_->pending.end(),
                              std::begin(RAJA_MAGIC_BYTES), std::end(RAJA_MAGIC_BYTES));
        impl_->pending.insert(impl_->pending.end(),
                              payload_after_magic, payload_after_magic + PAYLOAD_SIZE);

        if (impl_->written.fetch_add(1, std::memory_order_relaxed) == 0)
            impl_->first_utc_ms = utc_ms;
        impl_->last_utc_ms = utc_ms;
    }
    impl_->queue_ready.notify_one();
}

bool TelemetryLogWriter::is_active() const
{
    return impl_ != nullptr;
}

const std::filesystem::path& TelemetryLogWriter::file_path() const
{
    static const std::filesystem::path empty;
    return impl_ ? impl_->path : empty;
}

uint64_t TelemetryLogWriter::written_records() const
{
    return impl_ ? impl_->written.load(std::memory_order_relaxed) : 0;
}

uint64_t TelemetryLogWriter::dropped_records() const
{
    return impl_ ? impl_->dropped.load(std::memory_order_relaxed) : 0;
}

}
