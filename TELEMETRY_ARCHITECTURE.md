# АРХИТЕКТУРА ТЕЛЕМЕТРИИ - ДОКУМЕНТАЦИЯ

## Обзор системы

Система использует **единую точку обработки** для всех источников данных телеметрии:
- ✅ Симуляция (тестовые данные)
- ✅ Реальные данные с ESP32 (COM-порт)
- ✅ Сетевые клиенты (будущее расширение)

---

## Поток данных

```
┌──────────────────────────────────────────────────────────┐
│                  ИСТОЧНИКИ ДАННЫХ                        │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  [ESP32 COM-порт] ───┐     [Симуляция] ───┐             │
│                      │                     │             │
│  realDataThreadWorker()  simulationThreadWorker()       │
│         │                        │                       │
│         └────────────┬───────────┘                       │
│                      ▼                                   │
│          ┌────────────────────────┐                      │
│          │ createTelemetryPacket()│                      │
│          └────────────────────────┘                      │
│                      │                                   │
└──────────────────────┼───────────────────────────────────┘
                       ▼
           ┌────────────────────────────┐
           │ processIncomingTelemetry() │  ◄── ЕДИНАЯ ТОЧКА ВХОДА
           └────────────────────────────┘
                       │
           ┌───────────┼────────────┐
           │           │            │
           ▼           ▼            ▼
    ┌──────────┐  ┌──────────┐  ┌────────────────────┐
    │g_vehicles│  │RaceManager│  │BroadcastToClients()│
    │ (update) │  │ (tracks) │  │ (если сервер вкл.) │
    └──────────┘  └──────────┘  └────────────────────┘
```

---

## Компоненты системы

### 1. **processIncomingTelemetry()** - Единая точка обработки

**Расположение:** `OpenGL\src\network\ESP32_Code.cpp`

**Функции:**
1. Обновляет `g_vehicles[packet.ID]` (создает новый или обновляет существующий)
2. Конвертирует GPS → метры → normalized координаты
3. **Автоматически** рассылает пакет всем клиентам через `BroadcastTelemetryToClients()`

**Используется:**
- ✅ Симуляцией (`simulationThreadWorker`)
- ✅ Реальными данными (`realDataThreadWorker`)
- ✅ Сетевыми клиентами (будущее)

```cpp
void processIncomingTelemetry(const TelemetryPacket& packet)
{
    // 1. Update local vehicle
    {
        std::lock_guard<std::mutex> lock(g_vehicles_mutex);
        // ... update g_vehicles[packet.ID] ...
    }
    
    // 2. Broadcast to network (если сервер запущен)
    BroadcastTelemetryToClients(packet);
}
```

---

### 2. **Симуляция** - Разделена на 5 функций (Single Responsibility)

**Расположение:** `OpenGL\src\network\ESP32_Code.cpp`

#### 2.1. `calculateCumulativeDistances()` - Расстояния вдоль трека
```cpp
auto [distances, total_length] = calculateCumulativeDistances(track);
// Возвращает: cumulative_distances[] и total_track_length
```

#### 2.2. `updateVehiclePhysics()` - Физика движения
```cpp
updateVehiclePhysics(
    currentDistance,      // Обновляется
    currentSpeedKph,      // Обновляется случайно каждые 3-8 сек
    ...,
    deltaTime,
    total_track_length,
    rng, speed_dist, duration_dist
);
// Обновляет: currentDistance, currentSpeedKph
```

#### 2.3. `interpolateTrackPosition()` - Интерполяция по треку
```cpp
glm::vec2 pos = interpolateTrackPosition(
    currentDistance,
    track_points,
    cumulative_distances
);
// Возвращает: normalized координаты (0.0-1.0)
```

#### 2.4. `convertNormalizedToGPS()` - GPS конверсия
```cpp
double lat, lon;
bool success = convertNormalizedToGPS(
    normalized_pos,
    lat,     // OUT
    lon      // OUT
);
// Конвертирует: normalized → метры (UTM) → GPS
```

#### 2.5. `createTelemetryPacket()` - Создание пакета
```cpp
TelemetryPacket packet = createTelemetryPacket(
    vehicle_id,
    latitude,
    longitude,
    speed_kph,
    track_progress
);
// Возвращает: готовый пакет для передачи
```

#### 2.6. `simulationThreadWorker()` - Главный поток (координирует все)
```cpp
while (true)
{
    // 1. Физика
    updateVehiclePhysics(...);
    
    // 2. Интерполяция
    glm::vec2 pos = interpolateTrackPosition(...);
    
    // 3. GPS
    convertNormalizedToGPS(...);
    
    // 4. Пакет
    TelemetryPacket packet = createTelemetryPacket(...);
    
    // 5. Обновить track_progress
    vehicle.m_track_progress = ...;
    
    // 6. ✅ Обработать через единую систему!
    processIncomingTelemetry(packet);
    
    sleep(50ms);
}
```

---

### 3. **Реальные данные с ESP32** - COM-порт

**Расположение:** `OpenGL\src\network\ESP32_Code.cpp`

#### 3.1. `openCOMPort()` - Открытие порта (уже существовало)
```cpp
if (openCOMPort("COM3")) {
    // Порт открыт
}
```

#### 3.2. `realDataThreadWorker()` - Чтение COM-порта
```cpp
static void realDataThreadWorker(const std::string& com_port)
{
    if (!openCOMPort(com_port)) return;
    
    while (true)
    {
        // 1. Читаем заголовок (4 байта)
        uint32_t header = ...;
        
        // 2. Проверяем тип пакета
        if (header == PacketType::TELEMETRY)
        {
            TelemetryPacket packet;
            serial.readBytes(...);  // Читаем остальные данные
            
            // 3. ✅ Обрабатываем через единую систему!
            processIncomingTelemetry(packet);
        }
    }
}
```

#### 3.3. `startRealDataCapture()` - Запуск потока (public API)
```cpp
void startRealDataCapture(const std::string& com_port)
{
    std::thread real_thread(realDataThreadWorker, com_port);
    real_thread.detach();
}

// Использование:
startRealDataCapture("COM3");  // Запускает фоновый поток чтения
```

---

### 4. **Сервер** - Рассылка данных клиентам

**Расположение:** `OpenGL\src\network\Server.cpp`

#### 4.1. `BroadcastTelemetryToClients()` - Рассылка пакетов
```cpp
void BroadcastTelemetryToClients(const TelemetryPacket& packet)
{
#if NETWORKING_ENABLED
    if (!g_is_server_running) return;
    
    // Проверяем наличие аутентифицированных клиентов
    bool has_clients = false;
    for (const auto& pair : g_authenticated_connections) {
        if (pair.second) {
            has_clients = true;
            break;
        }
    }
    
    if (has_clients) {
        SendToAll(&packet, sizeof(TelemetryPacket));
    }
#endif
}
```

**Вызывается автоматически** из `processIncomingTelemetry()`, поэтому:
- ✅ Симулированные машины → рассылаются клиентам
- ✅ Реальные данные с ESP32 → рассылаются клиентам
- ✅ Единая логика, не нужно дублировать код!

---

## Как работает ОДНОВРЕМЕННО с тестовыми и реальными данными

### Сценарий 1: Только симуляция (клавиша 'T')
```
1. Нажимаем 'T' → создается машина
2. simulateVehicleMovement(id, track) запускает поток симуляции
3. simulationThreadWorker() → createTelemetryPacket() → processIncomingTelemetry()
4. Обновляется g_vehicles[id]
5. Если сервер запущен → BroadcastTelemetryToClients() отправляет клиентам
```

### Сценарий 2: Только реальные данные (ESP32)
```
1. Вызываем startRealDataCapture("COM3")
2. realDataThreadWorker() читает из COM-порта
3. Получен пакет → processIncomingTelemetry()
4. Обновляется g_vehicles[packet.ID]
5. Если сервер запущен → BroadcastTelemetryToClients() отправляет клиентам
```

### Сценарий 3: ОДНОВРЕМЕННО симуляция + реальные данные
```
1. startRealDataCapture("COM3")  // Фоновый поток читает ESP32
2. Нажимаем 'T' несколько раз   // Создаем симулированные машины

Результат:
- Реальные машины (ID из ESP32) + Симулированные машины (ID из симуляции)
- Все обрабатываются через processIncomingTelemetry()
- Все отображаются в g_vehicles
- Все рассылаются клиентам (если сервер запущен)
- RaceManager видит ВСЕ машины одинаково!
```

---

## Преимущества новой архитектуры

### 1. **Single Responsibility Principle**
Каждая функция делает **одну задачу**:
- `updateVehiclePhysics()` - только физика
- `interpolateTrackPosition()` - только интерполяция
- `convertNormalizedToGPS()` - только GPS конверсия
- `createTelemetryPacket()` - только создание пакета
- `processIncomingTelemetry()` - только обработка и распространение

### 2. **Единая точка обработки**
- Один код для всех источников данных
- Легко добавить новый источник (например, сетевой клиент)
- Автоматическая рассылка через сервер

### 3. **Тестирование без железа**
- Симуляция работает без ESP32
- Можно тестировать RaceManager, UI, сервер без реального оборудования

### 4. **Простое переключение режимов**
```cpp
// Режим 1: Только симуляция
simulateVehicleMovement(1, track);
simulateVehicleMovement(2, track);

// Режим 2: Только реальные данные
startRealDataCapture("COM3");

// Режим 3: Смешанный
startRealDataCapture("COM3");          // Реальная машина #1
simulateVehicleMovement(100, track);   // Симулированная машина #100
simulateVehicleMovement(101, track);   // Симулированная машина #101
```

---

## API для использования

### Запуск симуляции (существующий код)
```cpp
// В main.cpp уже есть:
if (key == GLFW_KEY_T && action == GLFW_PRESS) {
    static int vehicle_id = 1;
    g_vehicles[vehicle_id] = Vehicle(vehicle_id);
    simulateVehicleMovement(vehicle_id, g_smooth_track_points);
    vehicle_id++;
}
```

### Запуск чтения реальных данных (новое)
```cpp
// Добавить в main.cpp (например, клавиша 'R'):
if (key == GLFW_KEY_R && action == GLFW_PRESS) {
    startRealDataCapture("COM3");  // Запускает фоновый поток
    std::cout << "Started reading real data from COM3" << std::endl;
}
```

### Запуск сервера (существующий код)
```cpp
// В main.cpp уже есть:
if (key == GLFW_KEY_S && action == GLFW_PRESS && g_control == GLFW_PRESS) {
    if (!isServerRunning()) {
        std::thread server(serverWork);
        server.detach();
        ChangeisServerRunning();
    }
}
```

---

## Структура файлов

```
OpenGL\src\network\
├── ESP32_Code.h            // Объявления API
├── ESP32_Code.cpp          // Реализация:
│   ├── processIncomingTelemetry()        ◄── ЕДИНАЯ ТОЧКА
│   ├── realDataThreadWorker()            // Чтение COM-порта
│   ├── startRealDataCapture()            // Public API
│   ├── calculateCumulativeDistances()    // Вспомогательная функция
│   ├── updateVehiclePhysics()            // Вспомогательная функция
│   ├── interpolateTrackPosition()        // Вспомогательная функция
│   ├── convertNormalizedToGPS()          // Вспомогательная функция
│   ├── createTelemetryPacket()           // Вспомогательная функция
│   └── simulationThreadWorker()          // Главный поток симуляции
│
├── Server.h                // Объявления сервера
└── Server.cpp              // Реализация сервера:
    ├── BroadcastTelemetryToClients()     // Рассылка клиентам
    ├── serverWork()                      // Главный поток сервера
    └── (остальной код сервера без изменений)
```

---

## Будущие расширения

### Сетевой клиент (получение данных от другого ПК)
```cpp
// В будущем можно добавить:
static void clientReceiveThreadWorker(const std::string& server_ip, int port)
{
    while (true)
    {
        TelemetryPacket packet;
        int received = recvfrom(sock, &packet, sizeof(packet), ...);
        
        if (received == sizeof(packet)) {
            // ✅ Та же самая функция обработки!
            processIncomingTelemetry(packet);
        }
    }
}
```

Преимущество: **не нужно менять остальной код**, все работает автоматически!

---

## Заключение

✅ **Единая архитектура** - один путь для всех данных
✅ **Разделение ответственности** - каждая функция делает одну задачу
✅ **Легкое тестирование** - симуляция работает без железа
✅ **Автоматическая рассылка** - сервер автоматически отправляет все пакеты клиентам
✅ **Готово к расширению** - легко добавить новые источники данных

Сервер теперь работает **одновременно** с тестовыми и реальными данными через единую точку `processIncomingTelemetry()`. Никаких отдельных режимов в main.cpp не нужно!
