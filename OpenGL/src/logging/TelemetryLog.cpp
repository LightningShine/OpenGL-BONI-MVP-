#include "TelemetryLog.h"

#include "../input/Input.h"   // loaded_track_name

#include <atomic>
#include <cctype>
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

    // Сколько символов имени трассы попадает в имя файла. Ограничение не от
    // файловой системы, а от читаемости списка: длинное имя вытесняет из поля
    // зрения дату, ради которой список и сортируют.
    constexpr size_t MAX_TRACK_NAME_IN_FILE = 40;

    /// Имя трассы, пригодное для имени файла: всё, что не буква и не цифра,
    /// становится подчёркиванием. Байты старше 0x7F пропускаем как есть — имя
    /// пришло из файловой системы в той же узкой кодировке и вернётся в неё же.
    std::string sanitize_track_name(const std::string& name)
    {
        std::string result;
        result.reserve(name.size());
        for (const unsigned char symbol : name)
        {
            const bool allowed = std::isalnum(symbol) != 0 || symbol == '-' ||
                                 symbol == '_' || symbol >= 0x80;
            result.push_back(allowed ? static_cast<char>(symbol) : '_');
        }
        if (result.size() > MAX_TRACK_NAME_IN_FILE)
            result.resize(MAX_TRACK_NAME_IN_FILE);

        while (!result.empty() && result.back() == '_')
            result.pop_back();
        return result;
    }

    /// Имя файла: трасса, потом дата и время.
    ///
    /// Трасса стоит ПЕРВОЙ, потому что по списку записей надо видеть, к какой
    /// карте каждая относится: открыть запись на чужой трассе всё равно нельзя,
    /// а дата об этом не говорит ничего. Сортировка по времени внутри одной
    /// трассы сохраняется — ГГГГММДД_ЧЧММСС сортируется как число.
    std::string make_log_file_name(const std::string& track_name)
    {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &seconds);

        // Трассы может не быть (так пишет трекер) — тогда остаётся прежнее имя.
        std::string prefix = sanitize_track_name(track_name);
        if (prefix.empty())
            prefix = "telemetry";

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "_%04d%02d%02d_%02d%02d%02d.rjl",
                      local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec);
        return prefix + buffer;
    }

    /// Формат трассы по расширению файла.
    TrackFormat track_format_of(const std::filesystem::path& file)
    {
        std::string ext = file.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return (ext == ".trk2") ? TrackFormat::DualEdgeTrk2 : TrackFormat::CentreLineTxt;
    }

    /// Где кончаются записи. При встроенной трассе дальше идёт хвост, и читать
    /// его как пакеты нельзя — иначе и проверка, и повтор увидят мусор.
    uint64_t records_end_offset(const TelemetryLogHeader& header, uint64_t file_size)
    {
        if (header.track_embedded != 0 && header.record_count > 0)
        {
            const uint64_t end = sizeof(TelemetryLogHeader) +
                                 static_cast<uint64_t>(header.record_count) * RECORD_SIZE;
            if (end <= file_size)
                return end;
        }
        return file_size;
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

    std::error_code size_error;
    const uint64_t file_size = std::filesystem::file_size(path, size_error);
    const uint64_t records_end = records_end_offset(header, size_error ? UINT64_MAX : file_size);

    std::vector<uint8_t> record(RECORD_SIZE);
    rajagp::TelemetryPacket packet{};

    while (static_cast<uint64_t>(stream.tellg()) + RECORD_SIZE <= records_end &&
           stream.read(reinterpret_cast<char*>(record.data()), RECORD_SIZE))
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

    // Встроенная трасса из хвоста файла (см. TrackTrailerHeader).
    bool                 track_present = false;
    TrackFormat          track_format = TrackFormat::CentreLineTxt;
    std::vector<uint8_t> track_bytes;
    std::string          track_file_name;

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

    std::error_code size_error;
    const uint64_t file_size = std::filesystem::file_size(path, size_error);
    const uint64_t records_end = records_end_offset(impl_->header,
                                                    size_error ? UINT64_MAX : file_size);

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

    while (static_cast<uint64_t>(stream.tellg()) + RECORD_SIZE <= records_end &&
           stream.read(reinterpret_cast<char*>(record.data()), RECORD_SIZE))
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

    // Хвост с трассой. Его отсутствие — не ошибка: так пишет трекер на карту
    // памяти, и так же выглядят все записи, сделанные до появления хвоста.
    if (impl_->header.track_embedded != 0 && !size_error && records_end < file_size)
    {
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(records_end), std::ios::beg);

        TrackTrailerHeader trailer{};
        stream.read(reinterpret_cast<char*>(&trailer), sizeof(trailer));
        if (stream.gcount() == static_cast<std::streamsize>(sizeof(trailer)) &&
            trailer.magic == TRACK_TRAILER_MAGIC &&
            trailer.payload_size > 0 &&
            records_end + sizeof(trailer) + trailer.payload_size <= file_size)
        {
            impl_->track_bytes.resize(trailer.payload_size);
            stream.read(reinterpret_cast<char*>(impl_->track_bytes.data()), trailer.payload_size);
            if (stream.gcount() == static_cast<std::streamsize>(trailer.payload_size))
            {
                impl_->track_present = true;
                impl_->track_format = static_cast<TrackFormat>(trailer.format);
                impl_->track_file_name = std::string(
                    trailer.file_name,
                    strnlen(trailer.file_name, sizeof(trailer.file_name)));
            }
            else
            {
                impl_->track_bytes.clear();
            }
        }
        else
        {
            std::cerr << "[TELEMETRY-LOG] Embedded track is damaged, ignoring it" << std::endl;
        }
    }

    impl_->valid = true;
    std::cout << "[TELEMETRY-LOG] Loaded " << path.filename().string() << ": "
              << impl_->count() << " records, "
              << (duration_ms() / 1000.0) << "s"
              << (impl_->track_present ? ", track embedded" : "") << std::endl;
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

bool TelemetryLogReader::has_embedded_track() const
{
    return impl_ && impl_->track_present;
}

TrackFormat TelemetryLogReader::embedded_track_format() const
{
    return impl_ ? impl_->track_format : TrackFormat::CentreLineTxt;
}

const std::vector<uint8_t>& TelemetryLogReader::embedded_track_bytes() const
{
    static const std::vector<uint8_t> empty;
    return impl_ ? impl_->track_bytes : empty;
}

const std::string& TelemetryLogReader::embedded_track_file_name() const
{
    static const std::string empty;
    return impl_ ? impl_->track_file_name : empty;
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
    std::string track_name;

    // Копия файла трассы. Снимается ОДИН раз при открытии записи: трассу могут
    // сменить по ходу, а запись обязана нести ту, на которой сделана.
    std::vector<uint8_t> track_bytes;
    TrackFormat          track_format = TrackFormat::CentreLineTxt;
    std::string          track_file_name;

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
    // Хвост дописывается при закрытии, поэтому в промежуточном заголовке его
    // ещё нет: оборванная запись честно скажет, что трассы в ней нет.
    header.track_embedded = (finalize && !track_bytes.empty()) ? 1 : 0;

    // Имя трассы — чтобы запись знала, к чему относится. Без него файл с карты
    // памяти трекера остаётся набором координат, который не к чему привязать:
    // повтор требует загруженной трассы, а какой именно — приходилось помнить.
    // Поле фиксированной длины, поэтому имя обрезаем и всегда закрываем нулём.
    const std::string track = track_name;
    const size_t copied = track.copy(header.track_name, sizeof(header.track_name) - 1);
    header.track_name[copied] = 0;

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
                                       TelemetryLogSource source,
                                       const std::filesystem::path& track_file)
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
    impl_->track_name = loaded_track_name();

    if (!track_file.empty())
    {
        std::ifstream track(track_file, std::ios::in | std::ios::binary);
        if (track)
        {
            impl_->track_bytes.assign(std::istreambuf_iterator<char>(track),
                                      std::istreambuf_iterator<char>());
            impl_->track_format = track_format_of(track_file);
            impl_->track_file_name = track_file.filename().string();
        }
        else
        {
            std::cerr << "[TELEMETRY-LOG] Cannot read track " << track_file.string()
                      << ", recording without it" << std::endl;
        }
    }
    impl_->path = directory / make_log_file_name(impl_->track_name);
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

    // Порядок важен: сперва хвост в конец файла, потом заголовок — он ссылается
    // на хвост признаком track_embedded, и признак не должен появиться раньше
    // самих данных.
    if (!impl_->track_bytes.empty())
    {
        TrackTrailerHeader trailer{};
        trailer.magic = TRACK_TRAILER_MAGIC;
        trailer.version = 1;
        trailer.format = static_cast<uint16_t>(impl_->track_format);
        trailer.payload_size = static_cast<uint32_t>(impl_->track_bytes.size());
        const size_t copied = impl_->track_file_name.copy(trailer.file_name,
                                                          sizeof(trailer.file_name) - 1);
        trailer.file_name[copied] = 0;

        impl_->stream.seekp(0, std::ios::end);
        impl_->stream.write(reinterpret_cast<const char*>(&trailer), sizeof(trailer));
        impl_->stream.write(reinterpret_cast<const char*>(impl_->track_bytes.data()),
                            static_cast<std::streamsize>(impl_->track_bytes.size()));

        std::cout << "[TELEMETRY-LOG] Embedded track " << impl_->track_file_name
                  << " (" << impl_->track_bytes.size() << " bytes)" << std::endl;
    }

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
