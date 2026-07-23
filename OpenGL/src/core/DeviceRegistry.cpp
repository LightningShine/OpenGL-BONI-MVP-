#include "DeviceRegistry.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>

namespace {

int64_t now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// "group" — зарезервированное слово SQL, поэтому колонка называется dev_group.
constexpr const char* kCreateSql =
    "CREATE TABLE IF NOT EXISTS devices ("
    " id        INTEGER PRIMARY KEY,"
    " name      TEXT NOT NULL DEFAULT '',"
    " dev_group TEXT NOT NULL DEFAULT '',"
    " last_seen INTEGER NOT NULL DEFAULT 0);";

} // namespace

DeviceRegistry& DeviceRegistry::instance() {
    static DeviceRegistry reg;
    return reg;
}

DeviceRegistry::~DeviceRegistry() {
    // last_seen копится в памяти и сбрасывается в БД одним проходом при закрытии,
    // чтобы не писать на диск на каждом пакете телеметрии.
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        for (const auto& [id, d] : devices_) upsert_locked(d);
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool DeviceRegistry::open(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) return true; // уже открыт

    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[DeviceRegistry] Не удалось открыть БД " << db_path << ": "
                  << (db_ ? sqlite3_errmsg(db_) : "нет памяти") << "\n";
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }

    char* err = nullptr;
    if (sqlite3_exec(db_, kCreateSql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "[DeviceRegistry] Ошибка создания таблицы: " << (err ? err : "?") << "\n";
        sqlite3_free(err);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Загружаем все записи в память — дальше горячий путь работает без диска.
    devices_.clear();
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT id,name,dev_group,last_seen FROM devices;",
                           -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            DeviceInfo d;
            d.device_id      = sqlite3_column_int(st, 0);
            const auto* nm   = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            const auto* gr   = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
            d.name           = nm ? nm : "";
            d.group          = gr ? gr : "";
            d.last_seen_unix = sqlite3_column_int64(st, 3);
            d.seen_now       = false;
            devices_.emplace(d.device_id, std::move(d));
        }
    }
    sqlite3_finalize(st);

    std::cout << "[DeviceRegistry] Открыта БД " << db_path << ", записей: "
              << devices_.size() << "\n";
    return true;
}

void DeviceRegistry::upsert_locked(const DeviceInfo& d) {
    if (!db_) return;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT INTO devices(id,name,dev_group,last_seen) VALUES(?,?,?,?)"
            " ON CONFLICT(id) DO UPDATE SET"
            " name=excluded.name, dev_group=excluded.dev_group, last_seen=excluded.last_seen;",
            -1, &st, nullptr) != SQLITE_OK)
        return;
    sqlite3_bind_int  (st, 1, d.device_id);
    sqlite3_bind_text (st, 2, d.name.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 3, d.group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, d.last_seen_unix);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void DeviceRegistry::mark_seen(int32_t device_id) {
    if (device_id == 0) return; // 0 — «нет устройства», не заводим запись
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        // Новое устройство: заводим запись и сразу пишем в БД (это редкое событие,
        // а не каждый пакет), чтобы ID запомнился даже при аварийном выходе.
        DeviceInfo d;
        d.device_id      = device_id;
        d.seen_now       = true;
        d.last_seen_unix = now_unix_seconds();
        upsert_locked(d);
        devices_.emplace(device_id, std::move(d));
        return;
    }
    // Уже известно: обновляем только память (last_seen сбросится в БД при закрытии).
    it->second.seen_now       = true;
    it->second.last_seen_unix = now_unix_seconds();
}

std::optional<std::string> DeviceRegistry::name_of(int32_t device_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(device_id);
    if (it == devices_.end() || it->second.name.empty()) return std::nullopt;
    return it->second.name;
}

std::vector<DeviceInfo> DeviceRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DeviceInfo> out;
    out.reserve(devices_.size());
    for (const auto& [id, d] : devices_) out.push_back(d);
    return out;
}

void DeviceRegistry::set_name(int32_t device_id, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceInfo& d = devices_[device_id];
    if (d.device_id == 0) d.device_id = device_id; // была создана оператором []
    d.name = name;
    upsert_locked(d);
}

void DeviceRegistry::set_group(int32_t device_id, const std::string& group) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceInfo& d = devices_[device_id];
    if (d.device_id == 0) d.device_id = device_id;
    d.group = group;
    upsert_locked(d);
}

void DeviceRegistry::set_device_id(int32_t old_id, int32_t new_id) {
    if (old_id == new_id || new_id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(old_id);
    if (it == devices_.end()) return;
    DeviceInfo d = it->second;         // копия имени/группы/last_seen
    d.device_id  = new_id;
    devices_.erase(it);
    devices_[new_id] = d;
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM devices WHERE id=?;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, old_id);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    upsert_locked(d);
}

void DeviceRegistry::add_device(int32_t device_id, const std::string& name,
                                const std::string& group) {
    if (device_id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceInfo& d = devices_[device_id];
    d.device_id = device_id;
    d.name  = name;
    d.group = group;
    upsert_locked(d);
}

void DeviceRegistry::remove_device(int32_t device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.erase(device_id);
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM devices WHERE id=?;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, device_id);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
}

std::optional<int> DeviceRegistry::import_csv(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;

    auto trim = [](std::string s) -> std::string {
        size_t a = s.find_first_not_of(" \t\r\n\"");
        size_t b = s.find_last_not_of(" \t\r\n\"");
        return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };

    int loaded = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string idStr, name, group;
        std::getline(ss, idStr, ',');
        std::getline(ss, name,  ',');
        std::getline(ss, group, ',');
        idStr = trim(idStr);
        if (idStr.empty()) continue;

        // Строка-заголовок «id,name,group» — пропускаем (id не число).
        char* end = nullptr;
        long id = std::strtol(idStr.c_str(), &end, 10);
        if (end == idStr.c_str() || id == 0) continue;

        add_device(static_cast<int32_t>(id), trim(name), trim(group));
        ++loaded;
    }
    return loaded;
}
