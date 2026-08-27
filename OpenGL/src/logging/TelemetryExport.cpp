#include "TelemetryExport.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>

namespace logging
{
namespace
{
    // ------------------------------------------------------------------------
    // РЯД ФАЙЛА
    //
    // Всё, что выгрузка умеет писать, лежит здесь и только здесь — по одному
    // полю на канал, всё в double. Разные типы полей заставили бы таблицу
    // каналов ниже хранить не указатель на поле, а по функции на канал, и
    // «добавить канал» перестало бы быть одной строкой.
    //
    // Величины, которые форматы понимают ПО-РАЗНОМУ, лежат здесь дважды —
    // время и координаты. Это не дублирование данных, а два представления
    // одного замера: пересчитывать их в писателе значило бы, что каждый новый
    // формат приносит свою арифметику над координатами.
    // ------------------------------------------------------------------------
    struct Row
    {
        double sats = 0.0;
        double time_hhmmss = 0.0;     // UTC HHMMSS.SS — так время пишет VBOX
        double time_s = 0.0;          // секунды от начала выгрузки — так ждёт MoTeC
        double lat_min = 0.0;         // МИНУТЫ, + север
        double lon_min = 0.0;         // МИНУТЫ, + ЗАПАД (соглашение VBOX)
        double lat_deg = 0.0;         // градусы, + север
        double lon_deg = 0.0;         // градусы, + ВОСТОК (общее соглашение)
        double speed_kph = 0.0;
        double heading_deg = 0.0;
        double height_m = 0.0;
        double vert_vel_kph = 0.0;

        double distance_m = 0.0;      // от начала выгрузки
        double g_long = 0.0;
        double g_lat = 0.0;
        double g_sum = 0.0;
        double brake_g = 0.0;
        double accel_ms2 = 0.0;
        double lap = 0.0;
        double lap_time_s = 0.0;
        double lap_distance_m = 0.0;
        double fix_type = 0.0;
    };

    struct Channel
    {
        // Имена в двух форматах. nullptr — канала в этом формате нет: так
        // разведены величины, которые форматы держат по-своему (время, широта,
        // долгота), и только они.
        const char* vbo_header;   // строка секции [header] — как канал зовётся в VBOX
        const char* vbo_column;   // короткое имя в секции [column names]
        const char* csv_name;     // имя канала в шапке CSV

        // Единица. ДЛЯ .vbo ПУСТОЙ БЫТЬ НЕ МОЖЕТ: секции файла разделены пустой
        // строкой, и канал без единицы оборвал бы [channel units] на середине —
        // дальше разбор поехал бы по колонкам со сдвигом. У безразмерных
        // каналов пишем, ЧЕМ они меряны: count, lap, state.
        const char* unit;
        const char* format;       // printf-формат значения
        double Row::* value;
    };

    // ------------------------------------------------------------------------
    // КАНАЛЫ — ОДНА ТАБЛИЦА НА ОБА ФОРМАТА.
    //
    // Порядок строк задаёт порядок колонок в файле: и шапки, и данные
    // печатаются циклом по ЭТОЙ таблице. Разъехаться им поэтому нечем — а
    // разъехавшийся файл читается как «скорость в колонке курса» и
    // правдоподобно врёт.
    //
    // ДОБАВИТЬ КАНАЛ (тормоз, обороты, температура, что угодно) — три шага:
    //   1) поле в Row;
    //   2) строка в этой таблице;
    //   3) одна строчка заполнения в build_row().
    // Канал появится сразу в обоих форматах.
    //
    // Первые ВОСЕМЬ каналов .vbo обязательны для VBOX и идут строго в этом
    // порядке: анализаторы находят позицию и скорость по месту в файле, а не по
    // имени. Всё, что после них, — наши каналы, и здесь можно как угодно.
    // ------------------------------------------------------------------------
    constexpr Channel CHANNELS[] = {
        // vbo header               vbo column  csv name           unit    format    поле
        // Время у CSV идёт ПЕРВЫМ: i2 ждёт ось времени в первой колонке. В
        // .vbo этот канал не попадает (имени нет), поэтому обязательный порядок
        // VBOX — satellites первым — от такой перестановки не страдает.
        { nullptr,                  nullptr,    "Time",            "s",    "%.3f",   &Row::time_s },
        { "satellites",             "sats",     "Satellites",      "count","%03.0f", &Row::sats },
        { "time",                   "time",     nullptr,           "s",    "%09.2f", &Row::time_hhmmss },
        { "latitude",               "lat",      nullptr,           "min",  "%+.5f",  &Row::lat_min },
        { "longitude",              "long",     nullptr,           "min",  "%+.5f",  &Row::lon_min },
        { nullptr,                  nullptr,    "GPS Latitude",    "deg",  "%+.7f",  &Row::lat_deg },
        { nullptr,                  nullptr,    "GPS Longitude",   "deg",  "%+.7f",  &Row::lon_deg },
        { "velocity kmh",           "velocity", "Ground Speed",    "km/h", "%.3f",   &Row::speed_kph },
        { "heading",                "heading",  "GPS Heading",     "deg",  "%.2f",   &Row::heading_deg },
        { "height",                 "height",   "GPS Altitude",    "m",    "%+.2f",  &Row::height_m },
        { "vertical velocity kmh",  "vert-vel", "GPS Vert Speed",  "km/h", "%+.3f",  &Row::vert_vel_kph },

        { "distance",               "distance", "Distance",        "m",    "%.3f",   &Row::distance_m },
        { "LongitudinalAcceleration","LongAcc", "G Force Long",    "g",    "%+.3f",  &Row::g_long },
        { "LateralAcceleration",    "LatAcc",   "G Force Lat",     "g",    "%+.3f",  &Row::g_lat },
        { "CombinedAcceleration",   "CombAcc",  "G Force Combined","g",    "%.3f",   &Row::g_sum },
        { "Brake",                  "Brake",    "Brake",           "g",    "%.3f",   &Row::brake_g },
        { "Acceleration",           "Accel",    "Acceleration",    "m/s2", "%+.3f",  &Row::accel_ms2 },
        { "Lap",                    "Lap",      "Lap Number",      "lap",  "%.0f",   &Row::lap },
        { "LapTime",                "LapTime",  "Lap Time",        "s",    "%.3f",   &Row::lap_time_s },
        { "LapDistance",            "LapDist",  "Lap Distance",    "m",    "%.3f",   &Row::lap_distance_m },
        { "GpsFix",                 "GpsFix",   "GPS Fix",         "state","%.0f",   &Row::fix_type },
    };

    /// Замер телеметрии снимается с этим шагом (см. kTelemetrySampleInterval в
    /// RaceManager). Шапке MoTeC нужна частота; вычислять её из данных нельзя —
    /// в заезде есть пропуски, и средняя по файлу соврала бы.
    constexpr double SAMPLE_RATE_HZ = 10.0;

    /// Число спутников трекер не передаёт, а колонка обязательна: по ней
    /// анализаторы отбрасывают точки без решения. Поэтому печатаем не выдумку
    /// про спутники, а признак «решение есть» в единственном виде, который эта
    /// колонка умеет выразить. Само качество решения уходит каналом GpsFix.
    double sats_from_fix(int16_t fix_type)
    {
        return fix_type > 0 ? 12.0 : 0.0;
    }

    /// UTC-миллисекунды от полуночи → HHMMSS.SS, как их пишет VBOX.
    double vbo_time(uint32_t utc_ms)
    {
        const uint32_t hours   = utc_ms / 3'600'000u;
        const uint32_t minutes = (utc_ms / 60'000u) % 60u;
        const double   seconds = (utc_ms % 60'000u) / 1000.0;
        return hours * 10000.0 + minutes * 100.0 + seconds;
    }

    /// Замер → ряд файла. Дистанции и времени от начала выгрузки здесь ещё нет:
    /// они копятся по ходу и дописываются снаружи.
    Row build_row(const LapInfo& sample, int lap_number)
    {
        Row row;
        row.sats         = sats_from_fix(sample.fix_type);
        row.time_hhmmss  = vbo_time(sample.utc_ms);
        row.lat_deg      = sample.lat_dd;
        row.lon_deg      = sample.lon_dd;
        row.lat_min      = sample.lat_dd * 60.0;
        row.lon_min      = -sample.lon_dd * 60.0;   // + запад, см. заголовок
        row.speed_kph    = sample.speed;
        row.heading_deg  = sample.heading_deg;
        row.height_m     = 0.0;                     // высоту трекер не шлёт
        row.vert_vel_kph = 0.0;                     // и вертикальную скорость тоже

        row.g_long    = sample.gForceY;
        row.g_lat     = sample.gForceX;
        row.g_sum     = std::sqrt(static_cast<double>(sample.gForceX) * sample.gForceX +
                                  static_cast<double>(sample.gForceY) * sample.gForceY);
        // Своей педали тормоза у нас нет, и рисовать её «как в MoTeC» значило бы
        // показать инженеру число, которого никто не измерял. Замедление же
        // измерено: продольная перегрузка со знаком минус — это и есть
        // торможение. Тем же способом её показывает панель G-FORCE LONG.
        row.brake_g   = sample.gForceY < 0.f ? -sample.gForceY : 0.f;
        row.accel_ms2 = sample.aceleration;

        row.lap        = lap_number;
        row.lap_time_s = sample.timefromstart;
        row.fix_type   = sample.fix_type;
        return row;
    }

    /// Замеры → ряды файла. Общая часть обоих форматов: они различаются
    /// обвязкой, а не содержанием.
    std::vector<Row> build_rows(const ExportSamples& lap_samples)
    {
        std::vector<Row> rows;
        double total_distance = 0.0;
        double lap_distance   = 0.0;
        uint32_t first_utc_ms = 0;
        bool     have_first   = false;

        for (const auto& [lap_number, samples] : lap_samples)
        {
            lap_distance = 0.0;

            const LapInfo* previous = nullptr;
            for (const LapInfo& sample : samples)
            {
                // Без позиции ряд бесполезен: выгрузка — это прежде всего трек
                // по земле.
                if (sample.lat_dd == 0.0 && sample.lon_dd == 0.0)
                    continue;

                // Дистанцию интегрируем по скорости — так её считает и сам
                // VBOX. Длина трассы для этого не нужна, а значит, выгрузка не
                // зависит от того, загружена ли трасса и какая именно.
                if (previous != nullptr)
                {
                    const double dt = sample.timefromstart - previous->timefromstart;
                    if (dt > 0.0 && dt < 5.0)
                    {
                        const double step = (sample.speed + previous->speed) * 0.5 / 3.6 * dt;
                        total_distance += step;
                        lap_distance   += step;
                    }
                }
                previous = &sample;

                if (!have_first) { first_utc_ms = sample.utc_ms; have_first = true; }

                Row row = build_row(sample, lap_number);
                row.distance_m     = total_distance;
                row.lap_distance_m = lap_distance;
                // Ось времени файла — от первого замера, по меткам источника:
                // складывать времена кругов нельзя, между ними есть пропуски.
                row.time_s = (static_cast<double>(sample.utc_ms) -
                              static_cast<double>(first_utc_ms)) / 1000.0;
                rows.push_back(row);
            }
        }
        return rows;
    }

    /// Дата заезда для шапки. Знает её запись; сегодняшняя — запасной вариант:
    /// разбирают заезд обычно не в тот же день.
    void export_date(uint32_t date_yyyymmdd, int& day, int& month, int& year,
                     int& hour, int& minute, int& second)
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);

        day    = local.tm_mday;
        month  = local.tm_mon + 1;
        year   = local.tm_year + 1900;
        hour   = local.tm_hour;
        minute = local.tm_min;
        second = local.tm_sec;

        if (date_yyyymmdd != 0)
        {
            year  = static_cast<int>(date_yyyymmdd / 10000u);
            month = static_cast<int>((date_yyyymmdd / 100u) % 100u);
            day   = static_cast<int>(date_yyyymmdd % 100u);
        }
    }

    std::string format_value(const Channel& channel, const Row& row)
    {
        char cell[64];
        snprintf(cell, sizeof(cell), channel.format, row.*(channel.value));
        return cell;
    }

    bool fail(std::string* error, std::string reason)
    {
        std::cerr << "[EXPORT] " << reason << std::endl;
        if (error != nullptr) *error = std::move(reason);
        return false;
    }

    /// Общее начало обеих выгрузок: ряды собраны, файл открыт. Ряды собираем
    /// ЦЕЛИКОМ до открытия файла — пустая выгрузка не должна оставлять на диске
    /// обрубок, по которому потом гадают, тот это заезд или не тот.
    bool prepare(const std::filesystem::path& path, const ExportSamples& lap_samples,
                 std::vector<Row>& rows, std::ofstream& file, std::string* error)
    {
        if (error != nullptr) error->clear();

        rows = build_rows(lap_samples);
        if (rows.empty())
            return fail(error, "Nothing to export: no telemetry samples with a GPS position.");

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        file.open(path, std::ios::trunc);
        if (!file)
            return fail(error, "Cannot open the file for writing: " + path.string());

        return true;
    }

    bool finish(std::ofstream& file, const std::filesystem::path& path,
                size_t rows, size_t laps, std::string* error)
    {
        file.flush();
        if (!file)
            return fail(error, "Writing failed: " + path.string());

        std::cout << "[EXPORT] " << rows << " samples, " << laps << " laps -> "
                  << path.string() << std::endl;
        return true;
    }
}  // namespace

bool export_vbo(const std::filesystem::path& path,
                const ExportMeta& meta,
                const ExportSamples& lap_samples,
                std::string* error)
{
    std::vector<Row> rows;
    std::ofstream    file;
    if (!prepare(path, lap_samples, rows, file, error))
        return false;

    int day, month, year, hour, minute, second;
    export_date(meta.date_yyyymmdd, day, month, year, hour, minute, second);

    char created[64];
    snprintf(created, sizeof(created), "File created on %02d/%02d/%04d at %02d:%02d:%02d",
             day, month, year, hour, minute, second);
    file << created << "\n\n";

    file << "[header]\n";
    for (const Channel& channel : CHANNELS)
        if (channel.vbo_header != nullptr) file << channel.vbo_header << "\n";
    file << "\n";

    file << "[channel units]\n";
    for (const Channel& channel : CHANNELS)
        if (channel.vbo_header != nullptr) file << channel.unit << "\n";
    file << "\n";

    file << "[comments]\n";
    file << "Generated by RAJAGP Client\n";
    if (!meta.venue.empty())   file << "Venue: "   << meta.venue   << "\n";
    if (!meta.vehicle.empty()) file << "Vehicle: " << meta.vehicle << "\n";
    if (!meta.driver.empty())  file << "Driver: "  << meta.driver  << "\n";
    file << "Longitude sign convention: positive west (VBOX)\n";
    file << "\n";

    file << "[column names]\n";
    bool first = true;
    for (const Channel& channel : CHANNELS)
    {
        if (channel.vbo_column == nullptr) continue;
        file << (first ? "" : " ") << channel.vbo_column;
        first = false;
    }
    file << "\n\n";

    file << "[data]\n";
    for (const Row& row : rows)
    {
        first = true;
        for (const Channel& channel : CHANNELS)
        {
            if (channel.vbo_header == nullptr) continue;
            file << (first ? "" : " ") << format_value(channel, row);
            first = false;
        }
        file << "\n";
    }

    return finish(file, path, rows.size(), lap_samples.size(), error);
}

bool export_motec_csv(const std::filesystem::path& path,
                      const ExportMeta& meta,
                      const ExportSamples& lap_samples,
                      std::string* error)
{
    std::vector<Row> rows;
    std::ofstream    file;
    if (!prepare(path, lap_samples, rows, file, error))
        return false;

    int day, month, year, hour, minute, second;
    export_date(meta.date_yyyymmdd, day, month, year, hour, minute, second);

    char buffer[128];

    // ── Шапка: пары «ключ, значение» в кавычках ──────────────────────────────
    // Печатаем ВСЕ ключи, в том числе пустые. Импорт i2 ищет их по имени, и
    // пропущенная строка сдвигает разбор так же, как пропущенная колонка.
    file << "\"Format\",\"MoTeC CSV File\"\n";
    file << "\"Venue\",\""   << meta.venue   << "\"\n";
    file << "\"Vehicle\",\"" << meta.vehicle << "\"\n";
    file << "\"Driver\",\""  << meta.driver  << "\"\n";
    file << "\"Device\",\"RAJAGP\"\n";
    file << "\"Comment\",\"Exported by RAJAGP Client\"\n";

    snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d", day, month, year);
    file << "\"Log Date\",\"" << buffer << "\"\n";
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hour, minute, second);
    file << "\"Log Time\",\"" << buffer << "\"\n";

    snprintf(buffer, sizeof(buffer), "%.3f", SAMPLE_RATE_HZ);
    file << "\"Sample Rate\",\"" << buffer << "\"\n";
    snprintf(buffer, sizeof(buffer), "%.3f", rows.back().time_s);
    file << "\"Duration\",\"" << buffer << "\"\n";
    file << "\"Range\",\"All\"\n";
    file << "\n";

    // ── Имена каналов, затем единицы ─────────────────────────────────────────
    bool first = true;
    for (const Channel& channel : CHANNELS)
    {
        if (channel.csv_name == nullptr) continue;
        file << (first ? "" : ",") << "\"" << channel.csv_name << "\"";
        first = false;
    }
    file << "\n";

    first = true;
    for (const Channel& channel : CHANNELS)
    {
        if (channel.csv_name == nullptr) continue;
        file << (first ? "" : ",") << "\"" << channel.unit << "\"";
        first = false;
    }
    file << "\n\n";

    // ── Данные ───────────────────────────────────────────────────────────────
    for (const Row& row : rows)
    {
        first = true;
        for (const Channel& channel : CHANNELS)
        {
            if (channel.csv_name == nullptr) continue;
            file << (first ? "" : ",") << format_value(channel, row);
            first = false;
        }
        file << "\n";
    }

    return finish(file, path, rows.size(), lap_samples.size(), error);
}

}  // namespace logging
