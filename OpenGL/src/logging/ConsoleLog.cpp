#include "ConsoleLog.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <vector>

namespace logging
{
namespace
{
    // Обрезаем аномально длинные строки: журнал должен пережить любой мусор,
    // случайно попавший в поток вывода, не съев при этом память.
    constexpr size_t MAX_LINE_LENGTH = 4096;

    constexpr const char* LOG_FILE_PREFIX = "console_";
    constexpr const char* LOG_FILE_EXTENSION = ".log";

    // Как часто сбрасывать обычный вывод на диск. Компромисс: при жёстком
    // завершении теряется не больше этого окна, зато печать перестаёт стоить
    // обращения к диску. Поток ошибок сбрасывается всегда сразу.
    constexpr std::chrono::milliseconds FLUSH_INTERVAL{ 250 };

    /// Текущее локальное время как "ГГГГ-ММ-ДД ЧЧ:ММ:СС" или "ЧЧ:ММ:СС.ммм".
    std::string format_local_time(bool with_date)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % std::chrono::seconds(1);

        std::tm local{};
        localtime_s(&local, &seconds);

        char buffer[64];
        if (with_date)
        {
            std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                          local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                          local.tm_hour, local.tm_min, local.tm_sec);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
                          local.tm_hour, local.tm_min, local.tm_sec,
                          static_cast<int>(millis.count()));
        }
        return buffer;
    }

    /// Имя файла журнала: сортировка по имени совпадает с сортировкой по времени.
    std::string make_log_file_name()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

        std::tm local{};
        localtime_s(&local, &seconds);

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s%04d%02d%02d_%02d%02d%02d%s",
                      LOG_FILE_PREFIX,
                      local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec,
                      LOG_FILE_EXTENSION);
        return buffer;
    }

    /// Удаляет самые старые журналы, оставляя не больше keep_files штук.
    /// Ошибки файловой системы игнорируются: чистка — не повод падать.
    void remove_old_logs(const std::filesystem::path& directory, size_t keep_files)
    {
        if (keep_files == 0)
            return;

        std::error_code error;
        std::vector<std::filesystem::path> logs;

        for (const auto& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (error)
                return;
            if (!entry.is_regular_file(error))
                continue;

            const std::string name = entry.path().filename().string();
            if (name.rfind(LOG_FILE_PREFIX, 0) == 0 &&
                entry.path().extension() == LOG_FILE_EXTENSION)
            {
                logs.push_back(entry.path());
            }
        }

        if (logs.size() < keep_files)
            return;

        std::sort(logs.begin(), logs.end());
        const size_t remove_count = logs.size() - keep_files + 1;  // +1 — место под новый
        for (size_t i = 0; i < remove_count; ++i)
            std::filesystem::remove(logs[i], error);
    }

    // ------------------------------------------------------------------------
    // Файл журнала. Владеет потоком записи и своим мьютексом: в него пишут
    // одновременно cout и cerr, а те, в свою очередь, — из сетевого, serial-
    // и рендер-потоков.
    // ------------------------------------------------------------------------
    class LogFile
    {
    public:
        explicit LogFile(const std::filesystem::path& path)
            : stream_(path, std::ios::out | std::ios::trunc)
        {
        }

        bool is_open() const { return stream_.is_open(); }

        /// Пишет строку целиком под мьютексом — так строки из разных потоков не
        /// перемешиваются посимвольно.
        ///
        /// `urgent` — сбросить на диск немедленно. Так помечен поток ошибок:
        /// именно его содержимое нужно, если приложение не завершилось штатно.
        /// Обычный вывод сбрасывается пачками: сброс на каждой строке — это
        /// обращение к диску из потока, который в этот момент может рисовать
        /// кадр или вычерпывать COM-порт.
        void write_line(const char* tag, const std::string& text, bool urgent)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stream_.is_open())
                return;

            stream_ << '[' << format_local_time(false) << "] [" << tag << "] " << text << '\n';

            const auto now = std::chrono::steady_clock::now();
            if (urgent || (now - last_flush_) >= FLUSH_INTERVAL)
            {
                stream_.flush();
                last_flush_ = now;
            }
        }

    private:
        std::ofstream stream_;
        std::mutex mutex_;
        std::chrono::steady_clock::time_point last_flush_ = std::chrono::steady_clock::now();
    };

    // ------------------------------------------------------------------------
    // Буфер-тройник: отдаёт символы исходному буферу потока (консоль остаётся
    // как была) и параллельно копит строку для журнала.
    // ------------------------------------------------------------------------
    class TeeStreambuf final : public std::streambuf
    {
    public:
        /// `urgent` — сбрасывать строки на диск немедленно (для потока ошибок).
        TeeStreambuf(std::streambuf* console, LogFile& file, const char* tag, bool urgent)
            : console_(console), file_(file), tag_(tag), urgent_(urgent)
        {
        }

        /// Дописывает незавершённую строку, если поток закрылся без перевода строки.
        void flush_pending_line()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            emit_line_locked();
        }

    protected:
        int_type overflow(int_type ch) override
        {
            if (traits_type::eq_int_type(ch, traits_type::eof()))
                return traits_type::not_eof(ch);

            const char symbol = traits_type::to_char_type(ch);

            std::lock_guard<std::mutex> lock(mutex_);
            if (console_ != nullptr)
                console_->sputc(symbol);

            if (symbol == '\n')
                emit_line_locked();
            else if (symbol != '\r' && line_.size() < MAX_LINE_LENGTH)
                line_.push_back(symbol);

            return ch;
        }

        int sync() override
        {
            return (console_ != nullptr) ? console_->pubsync() : 0;
        }

    private:
        /// Отправляет накопленную строку в файл. Вызывающий держит mutex_.
        void emit_line_locked()
        {
            if (line_.empty())
                return;

            file_.write_line(tag_, line_, urgent_);
            line_.clear();
        }

        std::streambuf* console_;
        LogFile&        file_;
        const char*     tag_;
        bool            urgent_;
        std::mutex      mutex_;
        std::string     line_;
    };
}

// ----------------------------------------------------------------------------
// ConsoleLogSession
// ----------------------------------------------------------------------------
struct ConsoleLogSession::Impl
{
    std::filesystem::path path;
    std::unique_ptr<LogFile> file;
    std::unique_ptr<TeeStreambuf> out_buffer;
    std::unique_ptr<TeeStreambuf> err_buffer;
    std::streambuf* original_out = nullptr;
    std::streambuf* original_err = nullptr;
};

ConsoleLogSession::ConsoleLogSession(const std::filesystem::path& directory, size_t keep_files)
    : impl_(std::make_unique<Impl>())
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        // Сообщения журнала — ASCII: проект компилируется без /utf-8, и
        // кириллица в строковых литералах превратилась бы в '?' (см. README).
        std::cerr << "[LOG] Cannot create log directory " << directory.string()
                  << ": " << error.message() << std::endl;
        impl_.reset();
        return;
    }

    remove_old_logs(directory, keep_files);

    impl_->path = directory / make_log_file_name();
    impl_->file = std::make_unique<LogFile>(impl_->path);
    if (!impl_->file->is_open())
    {
        std::cerr << "[LOG] Cannot open log file " << impl_->path.string() << std::endl;
        impl_.reset();
        return;
    }

    impl_->file->write_line("LOG", "console log started " + format_local_time(true) + " (UTF-8)", true);

    // Перехват ставим последним: до этого момента любые сообщения об ошибках
    // должны идти в обычную консоль.
    impl_->original_out = std::cout.rdbuf();
    impl_->original_err = std::cerr.rdbuf();
    // Ошибки сбрасываются на диск сразу, обычный вывод — пачками: если
    // приложение упадёт, последняя строка stderr должна оказаться в файле.
    impl_->out_buffer = std::make_unique<TeeStreambuf>(impl_->original_out, *impl_->file, "OUT", false);
    impl_->err_buffer = std::make_unique<TeeStreambuf>(impl_->original_err, *impl_->file, "ERR", true);
    std::cout.rdbuf(impl_->out_buffer.get());
    std::cerr.rdbuf(impl_->err_buffer.get());
}

ConsoleLogSession::~ConsoleLogSession()
{
    if (!impl_)
        return;

    // Сначала возвращаем потокам их буферы, и только потом разрушаем наши:
    // иначе поток на мгновение остался бы с висячим указателем.
    if (impl_->original_out != nullptr)
        std::cout.rdbuf(impl_->original_out);
    if (impl_->original_err != nullptr)
        std::cerr.rdbuf(impl_->original_err);

    if (impl_->out_buffer)
        impl_->out_buffer->flush_pending_line();
    if (impl_->err_buffer)
        impl_->err_buffer->flush_pending_line();

    if (impl_->file)
        impl_->file->write_line("LOG", "console log closed " + format_local_time(true), true);
}

bool ConsoleLogSession::is_active() const
{
    return impl_ != nullptr;
}

const std::filesystem::path& ConsoleLogSession::file_path() const
{
    static const std::filesystem::path empty;
    return impl_ ? impl_->path : empty;
}

}
