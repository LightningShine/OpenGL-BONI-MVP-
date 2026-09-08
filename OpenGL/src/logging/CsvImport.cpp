#include "logging/CsvImport.h"

#include "logging/TelemetryLog.h"
#include "input/Input.h"          // loaded_track_path

#include <rajagp/Crc.h>
#include <rajagp/Protocol.h>
#include <rajagp/RajaParser.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>

namespace logging
{
namespace
{
    constexpr double GRAVITY = 9.80665;
    constexpr double PI      = 3.14159265358979323846;

    /// Сколько строк в начале файла просматриваем в поисках имён колонок.
    /// Шапки приборов бывают длинными, но не бесконечными.
    constexpr size_t HEADER_SEARCH_LINES = 60;

    /// Сколько строк данных оставляем в памяти. Их хватает и на определение
    /// формата времени, и на показ образца в окне привязки; всё остальное
    /// читается вторым проходом при конвертации.
    constexpr size_t PREVIEW_ROWS = 256;

    /// Меньше трёх колонок — это не таблица телеметрии, а пара
    /// «настройка, значение» из шапки прибора.
    constexpr size_t MIN_TABLE_COLUMNS = 3;

    // ── Мелочи разбора ───────────────────────────────────────────────────────

    std::string trim(std::string text)
    {
        const auto not_space = [](unsigned char c) { return !std::isspace(c) && c != '"'; };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
        text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
        return text;
    }

    std::string lowered(std::string text)
    {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    /// Одна строка → ячейки. Кавычки снимаются, разделитель внутри кавычек не
    /// считается разделителем: имена каналов у MoTeC как раз в кавычках.
    std::vector<std::string> split_row(const std::string& line, char delimiter)
    {
        std::vector<std::string> cells;
        std::string current;
        bool quoted = false;

        for (const char c : line)
        {
            if (c == '"')            { quoted = !quoted; continue; }
            if (c == delimiter && !quoted) { cells.push_back(trim(current)); current.clear(); continue; }
            current.push_back(c);
        }
        cells.push_back(trim(current));
        return cells;
    }

    /// Значение ячейки как число. `comma_decimal` — десятичная запятая: она
    /// встречается всюду, где разделитель колонок — точка с запятой.
    bool to_number(const std::string& cell, bool comma_decimal, double& out)
    {
        if (cell.empty()) return false;

        std::string text = cell;
        if (comma_decimal)
            for (char& c : text) if (c == ',') c = '.';

        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end == text.c_str()) return false;

        while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (end != nullptr && *end != '\0') return false;

        out = value;
        return true;
    }

    /// Доля ячеек строки, которые разбираются как числа.
    double numeric_share(const std::vector<std::string>& cells, bool comma_decimal)
    {
        if (cells.empty()) return 0.0;
        size_t numbers = 0;
        double value = 0.0;
        for (const std::string& cell : cells)
            if (to_number(cell, comma_decimal, value)) ++numbers;
        return static_cast<double>(numbers) / static_cast<double>(cells.size());
    }

    /// Разделитель — тот, при котором файл ЛУЧШЕ ВСЕГО выглядит таблицей:
    /// строки делятся на одинаковое число ячеек, и ячейки разбираются как
    /// числа.
    ///
    /// Считать вхождения символов, как здесь было раньше, нельзя. В европейском
    /// логе дроби пишут через запятую, поэтому запятых в файле больше, чем
    /// точек с запятой, — и строка `14:20:39,000;56,95;24,10` резалась по
    /// запятой на ячейки вроде «9500000;24», после чего таблица не находилась
    /// вовсе.
    char detect_delimiter(const std::vector<std::string>& lines)
    {
        const char candidates[] = { ',', ';', '\t' };

        char   best = ',';
        double best_score = -1.0;

        for (const char candidate : candidates)
        {
            std::vector<std::vector<std::string>> parsed;
            std::vector<size_t> widths;
            for (size_t i = 0; i < lines.size() && i < HEADER_SEARCH_LINES * 2; ++i)
            {
                if (lines[i].empty()) continue;
                parsed.push_back(split_row(lines[i], candidate));
                widths.push_back(parsed.back().size());
            }
            if (widths.empty()) continue;

            // Ширина таблицы — самая частая длина строки в ячейках. Шапка
            // прибора («Device;RaceLogger X») в неё не попадает и не мешает.
            size_t modal_width = 0, modal_count = 0;
            for (const size_t width : widths)
            {
                if (width < MIN_TABLE_COLUMNS) continue;
                const size_t count =
                    static_cast<size_t>(std::count(widths.begin(), widths.end(), width));
                if (count > modal_count) { modal_count = count; modal_width = width; }
            }
            if (modal_width == 0) continue;

            // Разделитель колонок и десятичный разделитель — одна региональная
            // настройка: где колонки делит не запятая, дроби пишут через неё.
            const bool comma_decimal = (candidate != ',');

            double score = 0.0;
            for (const std::vector<std::string>& row : parsed)
                if (row.size() == modal_width)
                    score += numeric_share(row, comma_decimal);

            if (score > best_score) { best_score = score; best = candidate; }
        }
        return best;
    }

    // ── Время ────────────────────────────────────────────────────────────────

    /// «14:20:39.100» → мс от полуночи. Часы и минуты обязательны.
    bool parse_time_of_day(const std::string& cell, double& ms)
    {
        int    hours = 0, minutes = 0;
        double seconds = 0.0;
        if (sscanf_s(cell.c_str(), "%d:%d:%lf", &hours, &minutes, &seconds) == 3)
        {
            ms = ((hours * 60.0 + minutes) * 60.0 + seconds) * 1000.0;
            return true;
        }
        if (sscanf_s(cell.c_str(), "%d:%lf", &minutes, &seconds) == 2)
        {
            ms = (minutes * 60.0 + seconds) * 1000.0;
            return true;
        }
        return false;
    }

    /// Значение колонки времени → миллисекунды от полуночи UTC.
    /// `first` — первое значение файла, нужно форматам, где время относительное.
    bool time_to_ms(const std::string& cell, CsvTimeFormat format, bool comma_decimal,
                    double first, double& ms)
    {
        if (format == CsvTimeFormat::TimeOfDay)
            return parse_time_of_day(cell, ms);

        double value = 0.0;
        if (!to_number(cell, comma_decimal, value)) return false;

        switch (format)
        {
            case CsvTimeFormat::SecondsFromStart:
                // Точки отсчёта в файле нет. Ставим заезд на полдень UTC: любое
                // время суток здесь одинаково условно, а полдень гарантированно
                // не переползает через полночь на заезде любой длины.
                ms = 12.0 * 3600.0 * 1000.0 + (value - first) * 1000.0;
                return true;

            case CsvTimeFormat::UtcHhmmss:
            {
                const int    hours   = static_cast<int>(value / 10000.0);
                const int    minutes = static_cast<int>(value / 100.0) % 100;
                const double seconds = value - hours * 10000.0 - minutes * 100.0;
                ms = ((hours * 60.0 + minutes) * 60.0 + seconds) * 1000.0;
                return true;
            }

            case CsvTimeFormat::UnixSeconds: ms = std::fmod(value * 1000.0, 86'400'000.0); return true;
            case CsvTimeFormat::UnixMillis:  ms = std::fmod(value,          86'400'000.0); return true;
            default: return false;
        }
    }

    CsvTimeFormat detect_time_format(const CsvTable& table, int column)
    {
        if (column < 0) return CsvTimeFormat::SecondsFromStart;
        const bool comma_decimal = table.comma_decimal;

        for (const std::vector<std::string>& row : table.rows)
        {
            if (column >= static_cast<int>(row.size())) continue;
            const std::string& cell = row[column];
            if (cell.empty()) continue;

            if (cell.find(':') != std::string::npos) return CsvTimeFormat::TimeOfDay;

            double value = 0.0;
            if (!to_number(cell, comma_decimal, value)) continue;

            if (value >= 1e12) return CsvTimeFormat::UnixMillis;
            if (value >= 1e9)  return CsvTimeFormat::UnixSeconds;
            // HHMMSS.ss: шесть цифр, минуты и секунды меньше шестидесяти. Просто
            // «больше десяти тысяч» сюда не годится — столько бывает и в
            // секундах от начала у длинной записи.
            if (value >= 10'000.0 && value < 240'000.0)
            {
                const int minutes = static_cast<int>(value / 100.0) % 100;
                const int seconds = static_cast<int>(value) % 100;
                if (minutes < 60 && seconds < 60) return CsvTimeFormat::UtcHhmmss;
            }
            return CsvTimeFormat::SecondsFromStart;
        }
        return CsvTimeFormat::SecondsFromStart;
    }

    // ── Геометрия ────────────────────────────────────────────────────────────

    /// Локальные метры от опорной точки. Для трассы длиной в километры
    /// равнопромежуточной проекции достаточно с запасом, а UTM тянуть сюда
    /// незачем: результат нужен только для производных скоростей.
    void to_local_meters(double lat, double lon, double lat0, double lon0, double& x, double& y)
    {
        constexpr double METERS_PER_DEGREE_LAT = 110'540.0;
        constexpr double METERS_PER_DEGREE_LON = 111'320.0;
        x = (lon - lon0) * METERS_PER_DEGREE_LON * std::cos(lat0 * PI / 180.0);
        y = (lat - lat0) * METERS_PER_DEGREE_LAT;
    }

    double wrap_angle(double radians)
    {
        while (radians >  PI) radians -= 2.0 * PI;
        while (radians < -PI) radians += 2.0 * PI;
        return radians;
    }

    bool fail(std::string* error, std::string reason)
    {
        std::cerr << "[CSV] " << reason << std::endl;
        if (error != nullptr) *error = std::move(reason);
        return false;
    }

    // ── Один замер, уже приведённый к числам ─────────────────────────────────
    struct Sample
    {
        double   time_s = 0.0;      // секунды от начала файла, монотонно
        uint32_t utc_ms = 0;        // мс от полуночи UTC
        double   lat = 0.0, lon = 0.0;
        double   speed_mps = 0.0;
        double   accel = 0.0;       // м/с²
        double   g_long = 0.0, g_lat = 0.0;
        int16_t  fix = 3;           // 3D-решение: чужой источник, не наш RTK
    };
}  // namespace

const char* csv_channel_label(CsvChannel channel)
{
    switch (channel)
    {
        case CsvChannel::Time:         return "Time";
        case CsvChannel::Latitude:     return "Latitude";
        case CsvChannel::Longitude:    return "Longitude";
        case CsvChannel::Speed:        return "Speed";
        case CsvChannel::Acceleration: return "Acceleration";
        case CsvChannel::GForceLong:   return "G force long";
        case CsvChannel::GForceLat:    return "G force lat";
        case CsvChannel::FixType:      return "GPS fix";
        default:                       return "";
    }
}

bool csv_channel_required(CsvChannel channel)
{
    return channel == CsvChannel::Time ||
           channel == CsvChannel::Latitude ||
           channel == CsvChannel::Longitude;
}

const char* csv_channel_hint(CsvChannel channel)
{
    switch (channel)
    {
        case CsvChannel::Time:         return "required";
        case CsvChannel::Latitude:     return "required";
        case CsvChannel::Longitude:    return "required";
        case CsvChannel::Speed:        return "else from position";
        case CsvChannel::Acceleration: return "else from speed";
        case CsvChannel::GForceLong:   return "else from speed";
        case CsvChannel::GForceLat:    return "else from path curvature";
        case CsvChannel::FixType:      return "else assumed 3D fix";
        default:                       return "";
    }
}

bool read_csv(const std::filesystem::path& path, CsvTable& table, std::string* error)
{
    if (error != nullptr) error->clear();
    table = CsvTable{};
    table.source = path;

    std::ifstream file(path);
    if (!file)
        return fail(error, "Cannot open the file: " + path.string());

    // Голову файла держим в памяти: по ней находятся разделитель, шапка и
    // формат времени, её же показывает окно привязки. Всё остальное только
    // пересчитывается на месте — файл целиком в память больше не кладём.
    constexpr size_t HEAD_LINES = HEADER_SEARCH_LINES + PREVIEW_ROWS;

    std::vector<std::string> lines;
    std::string line;
    while (lines.size() < HEAD_LINES && std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty())
        return fail(error, "The file is empty: " + path.string());

    table.delimiter     = detect_delimiter(lines);
    table.comma_decimal = (table.delimiter != ',');
    const bool comma_decimal = table.comma_decimal;

    // ── Где имена колонок ────────────────────────────────────────────────────
    // Ищем строку, которая сама числами не является, а НИЖЕ неё — строка с тем
    // же числом ячеек, которая числами является. Между ними допускаем одну
    // лишнюю: у MoTeC под именами каналов идёт строка единиц, и без этого
    // допуска шапка не нашлась бы вовсе.
    size_t header_index = std::string::npos;
    size_t first_data   = std::string::npos;

    for (size_t i = 0; i < lines.size() && i < HEADER_SEARCH_LINES; ++i)
    {
        const std::vector<std::string> cells = split_row(lines[i], table.delimiter);
        if (cells.size() < MIN_TABLE_COLUMNS) continue;
        if (numeric_share(cells, comma_decimal) > 0.5) continue;

        // Идём вниз до первой строки с числами. Пустые строки пропускаем, и
        // пропускаем ещё до MAX_UNIT_ROWS строк той же ширины без чисел — это
        // подписи единиц. Без такого допуска шапка находилась НЕ ТА: у нашего
        // же экспорта под именами каналов лежит строка единиц, а под ней пустая
        // строка, и «имена ровно на строку выше данных» выбирало единицы.
        constexpr int MAX_UNIT_ROWS = 2;
        int skipped_unit_rows = 0;

        for (size_t j = i + 1; j < lines.size(); ++j)
        {
            if (lines[j].empty()) continue;

            const std::vector<std::string> below = split_row(lines[j], table.delimiter);
            if (below.size() != cells.size()) break;

            if (numeric_share(below, comma_decimal) >= 0.5)
            {
                header_index = i;
                first_data   = j;
                break;
            }

            if (++skipped_unit_rows > MAX_UNIT_ROWS) break;
        }
        if (header_index != std::string::npos) break;
    }

    if (header_index == std::string::npos)
        return fail(error, "No table found: expected a row of column names followed by numeric rows.");

    table.columns = split_row(lines[header_index], table.delimiter);

    // Безымянная колонка всё равно должна быть выбираемой в окне привязки.
    for (size_t i = 0; i < table.columns.size(); ++i)
        if (table.columns[i].empty())
            table.columns[i] = "column " + std::to_string(i + 1);

    table.first_data_line = first_data;

    // Строки данных из головы — они же образец для окна привязки.
    for (size_t i = first_data; i < lines.size(); ++i)
    {
        if (lines[i].empty()) continue;
        std::vector<std::string> cells = split_row(lines[i], table.delimiter);
        if (cells.size() < table.columns.size()) continue;
        if (table.rows.size() < PREVIEW_ROWS) table.rows.push_back(cells);
        ++table.data_rows;
    }

    // Хвост файла ДОСЧИТЫВАЕМ, не сохраняя: оператору надо знать, сколько
    // строк он импортирует, а конвертации — сколько места занять под замеры.
    // Разбираем строку целиком, а не считаем разделители: в ячейке разделитель
    // может стоять внутри кавычек, и счёт по нему соврал бы.
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (split_row(line, table.delimiter).size() < table.columns.size()) continue;
        ++table.data_rows;
    }

    if (table.data_rows == 0)
        return fail(error, "The table has column names but no data rows.");

    std::cout << "[CSV] " << path.filename().string() << ": " << table.columns.size()
              << " columns, " << table.data_rows << " rows" << std::endl;
    return true;
}

CsvMapping guess_mapping(const CsvTable& table)
{
    CsvMapping mapping;

    // Имена, по которым узнаём колонку. Порядок важен: побеждает первое
    // совпадение, поэтому точные имена стоят раньше расплывчатых.
    struct Guess { CsvChannel channel; const char* needle; };
    static const Guess GUESSES[] = {
        { CsvChannel::Latitude,     "latitude" },
        { CsvChannel::Latitude,     "lat" },
        { CsvChannel::Longitude,    "longitude" },
        { CsvChannel::Longitude,    "long" },
        { CsvChannel::Longitude,    "lon" },
        { CsvChannel::Speed,        "ground speed" },
        { CsvChannel::Speed,        "speed" },
        { CsvChannel::Speed,        "velocity" },
        { CsvChannel::GForceLat,    "g force lat" },
        { CsvChannel::GForceLat,    "lateral" },
        { CsvChannel::GForceLat,    "latacc" },
        { CsvChannel::GForceLat,    "g_lat" },
        { CsvChannel::GForceLong,   "g force long" },
        { CsvChannel::GForceLong,   "longitudinal" },
        { CsvChannel::GForceLong,   "longacc" },
        { CsvChannel::GForceLong,   "g_long" },
        { CsvChannel::Acceleration, "acceleration" },
        { CsvChannel::Acceleration, "accel" },
        { CsvChannel::FixType,      "fix" },
        { CsvChannel::Time,         "utc" },
        { CsvChannel::Time,         "time" },
    };

    for (const Guess& guess : GUESSES)
    {
        if (mapping.of(guess.channel) != -1) continue;
        for (size_t i = 0; i < table.columns.size(); ++i)
        {
            const int index = static_cast<int>(i);
            // Колонку, уже отданную другой величине, второй раз не берём:
            // «lat» иначе утащил бы к себе «LatAcc».
            bool taken = false;
            for (size_t c = 0; c < static_cast<size_t>(CsvChannel::Count); ++c)
                if (mapping.column[c] == index) { taken = true; break; }
            if (taken) continue;

            if (lowered(table.columns[i]).find(guess.needle) != std::string::npos)
            {
                mapping.set(guess.channel, index);
                break;
            }
        }
    }

    mapping.time_format = detect_time_format(table, mapping.of(CsvChannel::Time));

    // Единица скорости — из имени колонки. Не нашли — километры в час: так
    // подписано у подавляющего большинства логгеров.
    mapping.speed_unit = CsvSpeedUnit::Kph;
    if (const int speed = mapping.of(CsvChannel::Speed); speed >= 0)
    {
        const std::string name = lowered(table.columns[speed]);
        if (name.find("m/s") != std::string::npos || name.find("mps") != std::string::npos)
            mapping.speed_unit = CsvSpeedUnit::Mps;
        else if (name.find("mph") != std::string::npos)
            mapping.speed_unit = CsvSpeedUnit::Mph;
    }

    return mapping;
}

/// Проходит по ВСЕМ строкам данных файла, отдавая каждую разобранную строку в
/// `fn`. Второй проход по файлу: таблица держит в памяти только голову.
///
/// Правила отбора строк те же, что и при чтении головы — иначе конвертация
/// увидела бы не то, что оператор видел в окне привязки.
template <typename Fn>
static bool for_each_data_row(const CsvTable& table, Fn&& fn, std::string* error)
{
    std::ifstream file(table.source);
    if (!file)
        return fail(error, "Cannot re-open the file: " + table.source.string());

    std::string line;
    size_t index = 0;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (index++ < table.first_data_line) continue;
        if (line.empty()) continue;

        const std::vector<std::string> cells = split_row(line, table.delimiter);
        if (cells.size() < table.columns.size()) continue;
        fn(cells);
    }
    return true;
}

bool convert_csv_to_replay(const CsvTable& table, const CsvMapping& mapping,
                           const std::filesystem::path& directory,
                           std::filesystem::path& out_path, std::string* error)
{
    if (error != nullptr) error->clear();

    const int time_column = mapping.of(CsvChannel::Time);
    const int lat_column  = mapping.of(CsvChannel::Latitude);
    const int lon_column  = mapping.of(CsvChannel::Longitude);
    if (time_column < 0 || lat_column < 0 || lon_column < 0)
        return fail(error, "Map Time, Latitude and Longitude before importing.");

    const bool comma_decimal = table.comma_decimal;

    const double speed_scale =
        mapping.speed_unit == CsvSpeedUnit::Mps ? 1.0 :
        mapping.speed_unit == CsvSpeedUnit::Mph ? 0.44704 : (1.0 / 3.6);

    // ── Таблица → замеры ─────────────────────────────────────────────────────
    const auto cell = [&](const std::vector<std::string>& row, int column) -> const std::string& {
        static const std::string empty;
        return (column >= 0 && column < static_cast<int>(row.size())) ? row[column] : empty;
    };

    double first_time = 0.0;
    bool   have_first_time = false;

    std::vector<Sample> samples;
    samples.reserve(table.data_rows);

    // Строки читаем ВТОРЫМ ПРОХОДОМ по файлу, а не из таблицы: в ней лежит
    // только голова (см. CsvTable). Замеры при этом остаются в памяти — они
    // маленькие, и без них не посчитать производные каналы центральной
    // разностью.
    const auto convert_row = [&](const std::vector<std::string>& row) {
        double lat = 0.0, lon = 0.0;
        if (!to_number(cell(row, lat_column), comma_decimal, lat)) return;
        if (!to_number(cell(row, lon_column), comma_decimal, lon)) return;
        // Строка без позиции — обрыв связи у логгера, а не точка на трассе.
        if (lat == 0.0 && lon == 0.0) return;

        // Точка отсчёта — ПЕРВАЯ РАЗОБРАВШАЯСЯ ячейка времени, а не первая
        // попавшаяся строка. Флаг раньше поднимался и тогда, когда число не
        // читалось: битая ячейка в первой же строке с координатами оставляла
        // first_time нулём, и для формата SecondsFromStart вся запись уезжала
        // от нуля вместо реального начала заезда. Строки, у которых время не
        // читается, всё равно отбрасываются ниже, так что пропуск безопасен.
        if (!have_first_time)
        {
            double raw = 0.0;
            if (to_number(cell(row, time_column), comma_decimal, raw))
            {
                first_time      = raw;
                have_first_time = true;
            }
        }

        double ms = 0.0;
        if (!time_to_ms(cell(row, time_column), mapping.time_format, comma_decimal, first_time, ms))
            return;

        Sample sample;
        sample.lat    = lat;
        sample.lon    = lon;
        sample.utc_ms = static_cast<uint32_t>(std::fmod(std::fmax(ms, 0.0), 86'400'000.0));

        double value = 0.0;
        if (to_number(cell(row, mapping.of(CsvChannel::Speed)), comma_decimal, value))
            sample.speed_mps = value * speed_scale;
        if (to_number(cell(row, mapping.of(CsvChannel::Acceleration)), comma_decimal, value))
            sample.accel = value;
        if (to_number(cell(row, mapping.of(CsvChannel::GForceLong)), comma_decimal, value))
            sample.g_long = value;
        if (to_number(cell(row, mapping.of(CsvChannel::GForceLat)), comma_decimal, value))
            sample.g_lat = value;
        if (to_number(cell(row, mapping.of(CsvChannel::FixType)), comma_decimal, value))
            sample.fix = static_cast<int16_t>(value);

        samples.push_back(sample);
    };

    if (!for_each_data_row(table, convert_row, error))
        return false;

    if (samples.size() < 2)
        return fail(error, "Not enough usable rows: need at least two with a position and a time.");

    // ── Ось времени ──────────────────────────────────────────────────────────
    // Считаем секунды от начала файла и разворачиваем переход через полночь:
    // метка источника обнуляется в полночь, а ось времени убывать не должна.
    {
        const uint32_t start = samples.front().utc_ms;
        double wrapped_days = 0.0;
        uint32_t previous = start;
        for (Sample& sample : samples)
        {
            if (sample.utc_ms + 1'000u < previous) wrapped_days += 86'400.0;
            previous = sample.utc_ms;
            sample.time_s = (static_cast<double>(sample.utc_ms) -
                             static_cast<double>(start)) / 1000.0 + wrapped_days;
        }
    }

    // ── Производные величины ─────────────────────────────────────────────────
    // Считаем ЦЕНТРАЛЬНОЙ разностью: односторонняя даёт сдвиг на половину шага,
    // и на графике торможение начиналось бы позже, чем на самом деле.
    const bool need_speed  = mapping.of(CsvChannel::Speed) < 0;
    const bool need_accel  = mapping.of(CsvChannel::Acceleration) < 0;
    const bool need_long   = mapping.of(CsvChannel::GForceLong) < 0;
    const bool need_lat    = mapping.of(CsvChannel::GForceLat) < 0;

    if (need_speed || need_accel || need_long || need_lat)
    {
        const double lat0 = samples.front().lat;
        const double lon0 = samples.front().lon;

        std::vector<double> x(samples.size()), y(samples.size()), heading(samples.size(), 0.0);
        for (size_t i = 0; i < samples.size(); ++i)
            to_local_meters(samples[i].lat, samples[i].lon, lat0, lon0, x[i], y[i]);

        for (size_t i = 1; i + 1 < samples.size(); ++i)
        {
            const double dt = samples[i + 1].time_s - samples[i - 1].time_s;
            if (dt <= 0.0) continue;

            const double dx = x[i + 1] - x[i - 1];
            const double dy = y[i + 1] - y[i - 1];
            if (need_speed)
                samples[i].speed_mps = std::sqrt(dx * dx + dy * dy) / dt;
            heading[i] = std::atan2(dx, dy);
        }
        if (samples.size() >= 3)
        {
            heading.front() = heading[1];
            heading.back()  = heading[samples.size() - 2];
            if (need_speed)
            {
                samples.front().speed_mps = samples[1].speed_mps;
                samples.back().speed_mps  = samples[samples.size() - 2].speed_mps;
            }
        }

        for (size_t i = 1; i + 1 < samples.size(); ++i)
        {
            const double dt = samples[i + 1].time_s - samples[i - 1].time_s;
            if (dt <= 0.0) continue;

            if (need_accel)
                samples[i].accel = (samples[i + 1].speed_mps - samples[i - 1].speed_mps) / dt;
            if (need_long)
                samples[i].g_long = samples[i].accel / GRAVITY;
            if (need_lat)
            {
                // Поперечное ускорение — скорость на угловую скорость поворота.
                // На стоянии курс не определён, и производная от шума дала бы
                // перегрузки на неподвижной машине.
                constexpr double MIN_SPEED_MPS = 1.0;
                samples[i].g_lat = (samples[i].speed_mps > MIN_SPEED_MPS)
                    ? samples[i].speed_mps * wrap_angle(heading[i + 1] - heading[i - 1]) / dt / GRAVITY
                    : 0.0;
            }
        }
    }

    // ── Замеры → запись ──────────────────────────────────────────────────────
    // Пишем ОБЫЧНЫЙ .rjl теми же пакетами, что приходят с приёмника: дальше
    // импортированный заезд неотличим от своего и идёт по тому же тракту.
    // Трассу в хвост кладём загруженную — как это делает живая запись.
    // Писатель ЖИВЁТ В БЛОКЕ: заголовок с числом записей и меткой последнего
    // пакета дописывается его деструктором. Открывать файл до этого — значит
    // открыть заведомо незаконченный.
    {
    TelemetryLogWriter writer(directory, TelemetryLogSource::Tracker, loaded_track_path());
    if (!writer.is_active())
        return fail(error, "Cannot create a replay file in " + directory.string());

    out_path = writer.file_path();

    uint8_t sequence = 0;
    for (const Sample& sample : samples)
    {
        rajagp::RajaTelemetryPacket wire{};
        wire.magic      = rajagp::PacketMagic::RAJA;
        wire.device_id  = static_cast<uint32_t>(mapping.vehicle_id);
        wire.seq        = sequence++;
        wire.gps_utc_ms = sample.utc_ms;
        wire.lat        = static_cast<int32_t>(std::llround(sample.lat * 1e7));
        wire.lon        = static_cast<int32_t>(std::llround(sample.lon * 1e7));
        wire.speed      = static_cast<uint32_t>(std::llround(std::fmax(sample.speed_mps, 0.0) * 3.6 * 100.0));
        // Ускорение и перегрузки на проводе беззнаковые, а по смыслу знаковые:
        // приём читает их обратно через static_cast к знаковому типу (см.
        // processIncomingTelemetry). Пишем ровно тем же приведением.
        wire.acceleration = static_cast<uint32_t>(static_cast<int32_t>(std::llround(sample.accel * 100.0)));
        wire.gForceX      = static_cast<uint16_t>(static_cast<int16_t>(std::llround(sample.g_lat  * 100.0)));
        wire.gForceY      = static_cast<uint16_t>(static_cast<int16_t>(std::llround(sample.g_long * 100.0)));
        wire.fix_type     = sample.fix;
        wire.crc = rajagp::crc16_ccitt_false(reinterpret_cast<const uint8_t*>(&wire),
                                             sizeof(wire) - sizeof(wire.crc));

        writer.write(reinterpret_cast<const uint8_t*>(&wire) + sizeof(wire.magic));
    }
    }

    const double duration = samples.back().time_s - samples.front().time_s;
    std::cout << "[CSV] Imported " << samples.size() << " samples ("
              << duration << "s) -> " << out_path.string() << std::endl;
    return true;
}

}  // namespace logging
