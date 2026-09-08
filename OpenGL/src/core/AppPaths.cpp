#include "core/AppPaths.h"

#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace app_paths
{
namespace
{
    const fs::path g_root{ "saves" };
    const fs::path g_tracks  = g_root / "tracks";
    const fs::path g_results = g_root / "results";
    const fs::path g_replays = g_root / "replays";
    const fs::path g_logs    = g_root / "logs";

    /// Куда отнести файл старой раскладки. Разбор идёт по имени и расширению:
    /// в прежних каталогах протоколы лежали вперемешку с трассами, и другого
    /// признака у них нет.
    const fs::path& destination_for(const fs::path& file)
    {
        const std::string name = file.filename().string();
        const std::string ext  = file.extension().string();

        if (name.rfind("VehicleResults", 0) == 0 || name.rfind("RaceResults", 0) == 0)
            return g_results;
        if (ext == ".rjl")
            return g_replays;
        if (ext == ".log")
            return g_logs;

        return g_tracks;   // .trk2 и .txt трасс
    }

    /// Переносит один файл. Занятое имя НЕ трогаем: файл остаётся на старом
    /// месте, а строка в журнале говорит, что с ним делать. Молча затирать
    /// чужой протокол заезда нельзя, а придумывать имя со счётчиком — значит
    /// плодить копии, о которых никто не узнает.
    bool move_file(const fs::path& from, const fs::path& to)
    {
        std::error_code error;
        if (fs::exists(to))
        {
            std::cout << "[PATHS] Skipped (target exists): " << from.string()
                      << " -> " << to.string() << std::endl;
            return false;
        }

        fs::rename(from, to, error);
        if (error)
        {
            // Разные тома: rename не сработает, копируем и удаляем исходник.
            fs::copy_file(from, to, error);
            if (error)
            {
                std::cerr << "[PATHS] Failed to move " << from.string() << ": "
                          << error.message() << std::endl;
                return false;
            }
            fs::remove(from, error);
        }
        return true;
    }

    /// Разбирает один каталог старой раскладки и удаляет его, если он опустел.
    void migrate_directory(const fs::path& legacy)
    {
        std::error_code error;
        if (!fs::is_directory(legacy, error))
            return;

        size_t moved = 0;
        std::vector<fs::path> files;
        for (const fs::directory_entry& entry : fs::directory_iterator(legacy, error))
        {
            if (entry.is_regular_file(error))
                files.push_back(entry.path());
        }

        for (const fs::path& file : files)
        {
            const fs::path& target_dir = destination_for(file);
            if (file.parent_path() == target_dir)
                continue;   // уже на месте

            if (move_file(file, target_dir / file.filename()))
                ++moved;
        }

        if (moved > 0)
        {
            std::cout << "[PATHS] Migrated " << moved << " file(s) from "
                      << legacy.string() << std::endl;
        }

        // Каталог удаляем только пустым: непереехавшие файлы остаются видны.
        fs::remove(legacy, error);
    }
}

const fs::path& root()    { return g_root; }
const fs::path& tracks()  { return g_tracks; }
const fs::path& results() { return g_results; }
const fs::path& replays() { return g_replays; }
const fs::path& logs()    { return g_logs; }

void prepare()
{
    std::error_code error;
    for (const fs::path* dir : { &g_tracks, &g_results, &g_replays, &g_logs })
    {
        fs::create_directories(*dir, error);
        if (error)
        {
            std::cerr << "[PATHS] Failed to create " << dir->string() << ": "
                      << error.message() << std::endl;
        }
    }

    // Разовый переезд со старой раскладки. Нужен установленным копиям: там
    // данные уже лежат по прежним каталогам, и без переноса трассы оператора
    // просто исчезли бы из списка. Повторные запуски бесплатны — каталогов
    // уже нет.
    migrate_directory("src/saves");
    migrate_directory("race saves");
    migrate_directory("logs");

    // Прежний корень протоколов совпадает с новым корнем данных, поэтому
    // каталог не удаляем — разбираем только файлы, лежащие в нём россыпью.
    std::error_code root_error;
    if (fs::is_directory(g_root, root_error))
    {
        std::vector<fs::path> files;
        for (const fs::directory_entry& entry : fs::directory_iterator(g_root, root_error))
        {
            if (entry.is_regular_file(root_error))
                files.push_back(entry.path());
        }
        for (const fs::path& file : files)
            move_file(file, destination_for(file) / file.filename());
    }
}

bool open_in_explorer(const fs::path& directory)
{
    std::error_code error;
    fs::create_directories(directory, error);

    const fs::path absolute = fs::absolute(directory, error);
    if (error)
        return false;

#ifdef _WIN32
    // ShellExecute, а не system("explorer"): не мигает консольным окном и не
    // зависит от разбора командной строки — путь может содержать пробелы.
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute.c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    return false;
#endif
}

}
