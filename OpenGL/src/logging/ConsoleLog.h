#pragma once

#include <filesystem>
#include <memory>
#include <string>

// ============================================================================
// Дублирование консольного вывода в файл.
//
// Весь диагностический вывод приложения идёт через std::cout / std::cerr, но
// консоль хранит лишь последние строки и умирает вместе с процессом. Сессия
// ниже подставляет свой streambuf, так что каждая строка попадает и в консоль,
// и в logs/console_<дата>_<время>.log с меткой времени. Разбирать, что
// сломалось на гонке, можно спустя дни.
//
// Ни один вызывающий код менять не нужно: перехват стоит на уровне потока
// вывода, а не на уровне обращений к нему.
// ============================================================================
namespace logging
{
    class ConsoleLogSession
    {
    public:
        /// Открывает файл журнала в `directory` (каталог создаётся) и включает
        /// дублирование std::cout/std::cerr. При неудаче объект остаётся
        /// неактивным, а вывод — обычным: журнал не имеет права ронять
        /// приложение. Хранит последние `keep_files` журналов, остальные удаляет.
        ConsoleLogSession(const std::filesystem::path& directory, size_t keep_files);

        /// Возвращает потокам их исходные буферы и закрывает файл.
        ~ConsoleLogSession();

        ConsoleLogSession(const ConsoleLogSession&) = delete;
        ConsoleLogSession& operator=(const ConsoleLogSession&) = delete;

        /// true, если дублирование включено.
        bool is_active() const;

        /// Путь к файлу журнала (пустой, если сессия неактивна).
        const std::filesystem::path& file_path() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
