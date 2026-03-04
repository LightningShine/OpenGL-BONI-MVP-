# 🎮 Quick Start Guide - Multiplayer Racing

## 🚀 Запуск сервера

1. **Загрузите карту трека**:
   - Drag & Drop `.txt` файл с GPS координатами
   - Или используйте UI → "Load Track File"

2. **Запустите сервер**:
   ```
   Нажмите: Shift + S
   ```
   
   Консоль покажет:
   ```
   [SERVER] Ready to broadcast telemetry
   [SERVER] Telemetry send rate: 60 Hz (every 16ms)
   Listening on port 777...
   ```

3. **Запустите симуляцию машин** (для тестирования):
   ```
   Нажмите: T  (создаёт новую машину)
   ```

## 💻 Подключение клиента

1. **Запустите клиент**:
   ```
   Нажмите: Shift + C
   ```

2. **Введите адрес сервера**:
   ```
   Enter server address:
   - 'd' или Enter = default (136.169.18.31:777)
   - 'l' = localhost (127.0.0.1:777)
   - Или полный адрес: 192.168.1.100:777
   ```

3. **Введите пароль**:
   ```
   Enter server password: mypassword123
   ```
   (По умолчанию в Config.h)

4. **Ожидайте синхронизации**:
   ```
   [CLIENT] ✅ Authentication successful!
   [CLIENT] 📡 Requesting map data from server...
   [CLIENT] 📦 Receiving map metadata:
     Origin: (...)
     Total points: 1234
     Total packets: 16
   [CLIENT] 📦 MapPointsPacket 1/16 received (80 points) [1/16 total]
   [CLIENT] 📦 MapPointsPacket 2/16 received (80 points) [2/16 total]
   ...
   [CLIENT] ✅ All map packets received! Assembling track...
   [CLIENT] ✅ Track loaded! Ready to receive telemetry.
   ```

5. **Машины появятся автоматически**!

## ⚠️ Важные замечания

### ❌ Сервер и клиент одновременно НЕ работают!
```
Если запущен сервер (Shift+S):
  → Shift+C выдаст ошибку: "Cannot start client - server mode is active!"

Если запущен клиент (Shift+C):
  → Shift+S выдаст ошибку: "Cannot start server - client mode is active!"
```

### ✅ Карта должна быть загружена!
```
Сервер:  Загрузите трек ПЕРЕД запуском сервера (Shift+S)
Клиент:  Карта загрузится автоматически с сервера
```

## 🔧 Настройки производительности

### Изменить частоту отправки (Config.h):
```cpp
namespace NetworkConstants {
    // Изменить с 60 Hz на желаемую частоту
    static constexpr int TELEMETRY_SEND_RATE_HZ = 60;  // 30, 60, 120...
}
```

### Изменить пароль (Config.h):
```cpp
namespace NetworkConstants {
    static constexpr const char* SERVER_PASSWORD = "your_password_here";
}
```

### Изменить таймауты (Config.h):
```cpp
namespace NetworkConstants {
    static constexpr int CONNECTION_TIMEOUT_MS = 5000;    // Отключение при потере связи
    static constexpr int MAP_REQUEST_TIMEOUT_MS = 10000;  // Таймаут загрузки карты
    static constexpr int MAX_MAP_RETRIES = 3;             // Количество повторов
}
```

## 🐛 Troubleshooting

### Проблема: "Authentication failed"
**Решение**: Проверьте пароль в `Config.h::NetworkConstants::SERVER_PASSWORD`

### Проблема: "Map sync failed after 3 retries"
**Возможные причины**:
1. Сервер не запущен
2. Брандмауэр блокирует порт 777
3. Плохое сетевое подключение

**Решение**:
```
1. Убедитесь что сервер работает (должно быть "Listening on port 777")
2. Откройте порт 777 в Windows Firewall
3. Попробуйте localhost (l) для локального тестирования
```

### Проблема: "Connection timeout - no packets for 5000ms"
**Решение**:
- Проверьте сетевое подключение
- Убедитесь что сервер не был остановлен
- Перезапустите клиент (Shift+C)

### Проблема: Машины дёргаются (jerky movement)
**Решение**:
1. Увеличьте TELEMETRY_SEND_RATE_HZ до 60-120 Hz
2. Проверьте ping до сервера (должен быть < 100ms)
3. Убедитесь что packet loss < 10%

## 📊 Статистика сети (консоль)

### Сервер показывает:
```
[SERVER] MapDataPacket sent (origin + 1234 points in 16 packets)
[SERVER] 📦 MapPointsPacket 1/16 sent (80 points)
...
[SERVER] ✅ Map sync complete!
```

### Клиент показывает:
```
[CLIENT] 📦 MapPointsPacket 1/16 received (80 points) [1/16 total]
[CLIENT] ⚠️ Duplicate packet #5 ignored  (при retransmission)
[CLIENT] ✅ All map packets received!
```

## 🎯 Проверка работоспособности

### ✅ Успешное подключение:
1. Сервер запущен → "Listening on port 777"
2. Клиент подключён → "✅ Authentication successful!"
3. Карта загружена → "✅ Track loaded!"
4. Машины видны → Треугольники/круги двигаются плавно

### ✅ Проверка телеметрии:
- Запустите машину на сервере (T)
- Клиент должен увидеть её через 1-2 секунды
- Машина должна двигаться плавно (60 FPS)

## 🔐 Безопасность

⚠️ **Внимание**: 
- Пароль передаётся в открытом виде (без шифрования)
- Используйте **только в доверенных сетях**!
- Для production: добавьте TLS/SSL шифрование

## 🎉 Готово!

Теперь у вас работает **полноценный multiplayer** с:
- ✅ Автоматической синхронизацией карты
- ✅ Real-time телеметрией (60 Hz)
- ✅ Плавным движением машин
- ✅ Устойчивостью к network errors

**Приятного использования!** 🏁
