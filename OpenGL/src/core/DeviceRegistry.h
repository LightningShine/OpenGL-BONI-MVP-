#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

struct sqlite3; // fwd — тянуть <sqlite3.h> в заголовок не нужно

/// Одна запись реестра трекеров.
struct DeviceInfo {
    int32_t     device_id      = 0;     // аппаратный ID трекера (packet.ID)
    std::string name;                   // заданное пользователем имя
    std::string group;                  // группа (заезд/команда/цвет)
    bool        seen_now       = false; // на связи в этой сессии
    int64_t     last_seen_unix = 0;     // время последнего пакета, unix-сек
};

/// Реестр устройств: имя и группа по ID трекера, с сохранением в SQLite.
///
/// Потокобезопасен: сетевой поток зовёт mark_seen/name_of на каждом пакете,
/// UI-поток читает snapshot() и правит записи. Всё состояние держится в памяти
/// (загружается из БД при open), поэтому чтения в горячем пути — это lookup по
/// map под коротким локом, БЕЗ обращения к диску. В БД пишем только при правках
/// пользователя и при первом появлении нового устройства.
///
/// Владеет соединением с БД по RAII: open() открывает, деструктор закрывает.
class DeviceRegistry {
public:
    static DeviceRegistry& instance();

    /// Открывает/создаёт файл БД и загружает записи в память.
    /// Возвращает false, если БД открыть не удалось (реестр останется пустым,
    /// приложение работает дальше — имена просто не будут сохраняться).
    bool open(const std::string& db_path);

    // ── горячий путь (сетевой поток) ─────────────────────────────────────────
    /// Помечает устройство «на связи»; заводит запись при первом появлении.
    void mark_seen(int32_t device_id);
    /// Заданное имя устройства, если оно есть (иначе пустой optional).
    std::optional<std::string> name_of(int32_t device_id) const;

    // ── UI-поток ─────────────────────────────────────────────────────────────
    std::vector<DeviceInfo> snapshot() const;   // копия всех записей для таблицы
    void set_name (int32_t device_id, const std::string& name);
    void set_group(int32_t device_id, const std::string& group);
    /// Переносит имя/группу со старого ID на новый (правка ID устройства).
    void set_device_id(int32_t old_id, int32_t new_id);
    void add_device(int32_t device_id, const std::string& name, const std::string& group);
    void remove_device(int32_t device_id);

    /// Импорт CSV-таблицы «id,name,group» (первая строка-заголовок пропускается,
    /// если в ней нет числа). Возвращает число загруженных строк или пустой
    /// optional при ошибке чтения файла.
    std::optional<int> import_csv(const std::string& path);

    ~DeviceRegistry();
    DeviceRegistry(const DeviceRegistry&)            = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;

private:
    DeviceRegistry() = default;

    /// Пишет одну запись в БД (INSERT OR REPLACE). Вызывать под локом.
    void upsert_locked(const DeviceInfo& d);

    mutable std::mutex                        mutex_;
    std::unordered_map<int32_t, DeviceInfo>   devices_;      // кэш = источник истины в рантайме
    sqlite3*                                  db_ = nullptr; // владеем; закрываем в деструкторе
};
