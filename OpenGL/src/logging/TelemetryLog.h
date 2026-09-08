#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Запись радиотелеметрии в файл (этап 1 системы повторов).
//
// В файл ложатся ИСХОДНЫЕ байты эфира — те же 37, что описаны в
// rajagp::RajaTelemetryPacket, вместе с CRC и счётчиком seq. Никакой
// трансляции: лог должен быть свидетелем, а не пересказом. Тот же формат
// пишет на SD-карту сам трекер, поэтому оба файла читает один парсер
// (rajagp::parseRajaPayload), а слить их можно простым слиянием по gps_utc_ms —
// метка абсолютная, из GNSS, одинаковая для всех устройств.
//
// Записи фиксированного размера, поэтому:
//   * позиция записи N вычисляется арифметикой, индексный файл не нужен;
//   * перемотка по времени — двоичный поиск прямо по файлу;
//   * файл, оборванный на полуслове (пропало питание), остаётся валидным:
//     хвостовая неполная запись отсекается по размеру, битая — по CRC.
// ============================================================================
namespace logging
{
#pragma pack(push, 1)
    struct TelemetryLogHeader
    {
        uint32_t magic;            // TELEMETRY_LOG_MAGIC
        uint16_t version;          // TELEMETRY_LOG_VERSION
        uint16_t record_size;      // sizeof(rajagp::RajaTelemetryPacket)
        uint32_t utc_date;         // ГГГГММДД: в gps_utc_ms есть время, но нет даты
        uint32_t first_utc_ms;     // метка первой записи
        uint32_t last_utc_ms;      // метка последней; 0 = запись оборвалась
        uint32_t record_count;     // 0 = считать по размеру файла
        uint8_t  source;           // TelemetryLogSource
        uint8_t  track_embedded;   // 1 = после заголовка лежит трек (пока всегда 0)
        char     track_name[38];   // трек сессии; пусто, если не был загружен
    };
#pragma pack(pop)
    static_assert(sizeof(TelemetryLogHeader) == 64, "Telemetry log header must stay 64 bytes");

    enum class TelemetryLogSource : uint8_t
    {
        Tracker  = 0,  // SD-карта трекера: одно устройство
        Receiver = 1,  // приёмник на ПК: все устройства вперемешку
    };

    static constexpr uint32_t TELEMETRY_LOG_MAGIC   = 0x314C4A52u;  // 'RJL1'
    static constexpr uint16_t TELEMETRY_LOG_VERSION = 1;

    // ------------------------------------------------------------------------
    // ТРАССА ВНУТРИ ЗАПИСИ
    //
    // Лежит ХВОСТОМ, после всех записей, а не сразу за заголовком. Причина одна
    // и она решающая: трекер пишет файл на карту памяти последовательно —
    // заголовок, дальше пакеты до выключения питания. Он не может ни знать
    // трассу заранее, ни подвинуть записи, чтобы вставить её в середину. Хвост
    // же дописывается кем угодно и когда угодно, не сдвигая ни одного байта:
    // трекер оставляет его пустым, а приложение добавляет при первом открытии.
    //
    // Начало хвоста вычисляется как 64 + record_count * record_size, поэтому
    // новых полей в заголовке не понадобилось: признак — track_embedded.
    //
    // Внутри лежит КОПИЯ ФАЙЛА ТРАССЫ как есть. Это не изобретение ещё одного
    // формата геометрии: файл разбирают те же загрузчики, что и трассу с диска,
    // поэтому второй реализации разбора не появляется.
    // ------------------------------------------------------------------------
    static constexpr uint32_t TRACK_TRAILER_MAGIC = 0x4B544A52u;   // 'RJTK'

    enum class TrackFormat : uint16_t
    {
        CentreLineTxt = 0,   // .txt — одна линия по DMS-координатам
        DualEdgeTrk2  = 1,   // .trk2 — два края, привязка по UTM
    };

#pragma pack(push, 1)
    struct TrackTrailerHeader
    {
        uint32_t magic;        // TRACK_TRAILER_MAGIC
        uint16_t version;      // 1
        uint16_t format;       // TrackFormat
        uint32_t payload_size; // байт файла трассы следом за этим заголовком
        char     file_name[52];// исходное имя файла, чтобы восстановить его как есть
    };
#pragma pack(pop)
    static_assert(sizeof(TrackTrailerHeader) == 64, "Track trailer header must stay 64 bytes");

    /// Итог проверки файла журнала.
    struct TelemetryLogStats
    {
        uint64_t records_total = 0;    // сколько записей прочитано
        uint64_t records_valid = 0;    // из них прошли CRC
        uint64_t trailing_bytes = 0;   // остаток неполной записи в конце
        bool     header_valid = false;
    };

    /// Перечитывает файл и проверяет каждую запись через rajagp::parseRajaPayload.
    /// Нужна и как самопроверка после записи, и как основа читателя.
    TelemetryLogStats verify_telemetry_log(const std::filesystem::path& path);

    // ------------------------------------------------------------------------
    // Читатель записи.
    //
    // Файл загружается целиком: час гонки на полном поле машин — около 130 МБ,
    // это меньше, чем занимает одна текстура, зато дальше любое обращение идёт
    // без диска. Записи фиксированной длины, поэтому запись N лежит по
    // вычисляемому смещению, а поиск по времени — двоичный, без индекса.
    // ------------------------------------------------------------------------
    class TelemetryLogReader
    {
    public:
        /// Открывает и проверяет файл. Записи с битым CRC и незавершённый хвост
        /// отбрасываются, поэтому наружу видны только целые пакеты.
        explicit TelemetryLogReader(const std::filesystem::path& path);
        ~TelemetryLogReader();

        TelemetryLogReader(const TelemetryLogReader&) = delete;
        TelemetryLogReader& operator=(const TelemetryLogReader&) = delete;

        bool is_open() const;
        const TelemetryLogHeader& header() const;
        size_t record_count() const;

        /// Байты записи `index`: ровно sizeof(rajagp::RajaTelemetryPacket),
        /// начиная с маркера 'RAJA'. nullptr, если индекс за пределами.
        const uint8_t* record(size_t index) const;

        /// Метка времени записи (gps_utc_ms). 0, если индекс за пределами.
        uint32_t record_utc_ms(size_t index) const;

        /// Время записи от НАЧАЛА файла, мс. В отличие от gps_utc_ms не
        /// обнуляется в полночь и не убывает — на нём строится темп повтора.
        uint32_t record_elapsed_ms(size_t index) const;

        /// Первая запись со временем не раньше `utc_ms`. Двоичный поиск по
        /// монотонной части файла; при переходе записи через полночь опирается
        /// на порядок следования, а не на голое сравнение меток.
        size_t find_index_at_or_after(uint32_t utc_ms) const;

        /// Длительность записи в миллисекундах (с учётом перехода через полночь).
        uint32_t duration_ms() const;

        /// В записи лежит трасса, на которой она сделана.
        bool has_embedded_track() const;

        /// Формат и содержимое встроенной трассы. Пусто, если её нет.
        TrackFormat embedded_track_format() const;
        const std::vector<uint8_t>& embedded_track_bytes() const;
        const std::string& embedded_track_file_name() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // ------------------------------------------------------------------------
    // Писатель. Открывает файл в конструкторе, дописывает заголовок и закрывает
    // в деструкторе. write() вызывается из потока чтения COM-порта и не
    // блокирует его на диске: запись идёт через очередь в отдельном потоке.
    // ------------------------------------------------------------------------
    class TelemetryLogWriter
    {
    public:
        /// Создаёт `directory` при необходимости и открывает новый файл.
        /// При неудаче объект остаётся неактивным, а приложение работает как
        /// прежде: журнал не имеет права ронять приём телеметрии.
        /// `track_file` — путь к файлу загруженной трассы. Его копия ляжет в хвост
        /// записи, чтобы повтор не зависел от того, есть ли эта трасса на той
        /// машине, где его будут смотреть. Пустой путь допустим: трассы может не
        /// быть (так пишет трекер), тогда хвоста просто не будет.
        TelemetryLogWriter(const std::filesystem::path& directory, TelemetryLogSource source,
                           const std::filesystem::path& track_file = {});
        ~TelemetryLogWriter();

        TelemetryLogWriter(const TelemetryLogWriter&) = delete;
        TelemetryLogWriter& operator=(const TelemetryLogWriter&) = delete;

        /// Ставит в очередь один пакет: `payload_after_magic` — ровно
        /// rajagp::kRajaPayloadAfterMagic байт, идущих за маркером 'RAJA'
        /// (то, что уже прошло проверку CRC в читателе порта).
        /// Не блокирует; при переполнении очереди запись отбрасывается и
        /// считается в dropped_records().
        void write(const uint8_t* payload_after_magic);

        bool is_active() const;
        const std::filesystem::path& file_path() const;
        uint64_t written_records() const;
        uint64_t dropped_records() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
