#pragma once

// Map and coordinate system constants
namespace MapConstants {
    static constexpr double MAP_SIZE = 100.0; // 100 meters = 1.0 in OpenGL coordinates
    static constexpr float MAP_BOUND_X = 2.0f;
    static constexpr float MAP_BOUND_Y = 2.0f;
}

// Vehicle rendering constants
namespace VehicleConstants {
    static constexpr float VEHICLE_BODY_RADIUS = 0.05f;      // 5 meters (радиус тела)
    static constexpr float VEHICLE_OUTLINE_WIDTH = 0.005f;   // ✅ Толщина обводки (белая рамка)
    static constexpr float VEHICLE_OUTLINE_RADIUS = VEHICLE_BODY_RADIUS + VEHICLE_OUTLINE_WIDTH;  // Автоматический расчёт
    static constexpr int VEHICLE_CIRCLE_SEGMENTS = 20;
    static constexpr int VEHICLE_TIMEOUT_MS = 5000;         // 5 seconds (was 300ms - too short!)
    static constexpr int AUTHORITATIVE_VEHICLE_TIMEOUT_MS = 750; // Remote processed-state vehicles disappear quickly after server stop

    // ✅ Цвет обводки (RGB)
    static constexpr float VEHICLE_OUTLINE_COLOR_R = 1.0f;
    static constexpr float VEHICLE_OUTLINE_COLOR_G = 1.0f;
    static constexpr float VEHICLE_OUTLINE_COLOR_B = 1.0f;
}

// Camera constants
namespace CameraConstants {
    static constexpr float CAMERA_MOVE_SPEED = 0.01f;
    static constexpr float CAMERA_ZOOM_MIN = 0.1f;
    static constexpr float CAMERA_ZOOM_MAX = 10.0f;
    static constexpr float CAMERA_FRICTION = 0.85f;
    
    // Rotation constants
    static constexpr float CAMERA_ROTATION_SPEED = 0.5f;  // degrees per frame
    static constexpr float CAMERA_ROTATION_MIN = -180.0f;  // degrees
    static constexpr float CAMERA_ROTATION_MAX = 180.0f;   // degrees
}

// Network constants
namespace NetworkConstants {
    static constexpr int DEFAULT_SERVER_PORT = 777;
    static constexpr const char* DEFAULT_SERVER_IP = "136.169.18.31";
    static constexpr const char* DEFAULT_LOCAL_SERVER_IP = "127.0.0.1";
    static constexpr const char* SERVER_PASSWORD = "mypassword123";  
    static constexpr int MAX_AUTH_ATTEMPTS = 10;
    // ✅ Network performance settings
    static constexpr int TELEMETRY_SEND_RATE_HZ = 60;  // 60 updates/sec (smooth)
    static constexpr int TELEMETRY_SEND_INTERVAL_MS = 1000 / TELEMETRY_SEND_RATE_HZ;

    // ✅ Connection reliability settings
    static constexpr int MAP_REQUEST_TIMEOUT_MS = 10000;  // 10 seconds to receive full map
    static constexpr int MAP_PACKET_TIMEOUT_MS = 2000;    // 2 seconds per packet
    static constexpr int CONNECTION_TIMEOUT_MS = 5000;     // 5 seconds no packets = disconnect
    static constexpr int MAX_MAP_RETRIES = 3;              // Retry map request 3 times
    static constexpr int MAX_POINTS_PER_MAP_PACKET = 80;   // Max GPS points per UDP packet

    static constexpr int SERVER_POLL_INTERVAL_MS = 10;
    static constexpr int CLIENT_POLL_INTERVAL_MS = 5;
    static constexpr int AUTH_POLL_INTERVAL_MS = 10;
}

namespace PacketMagic {
    static constexpr uint32_t AUTH = 0x41555448;        // 'AUTH'
    static constexpr uint32_t RESP = 0x52455350;        // 'RESP'
    static constexpr uint32_t DATA = 0x44415441;        // 'DATA'
    static constexpr uint32_t MAP_REQUEST = 0x4D415052; // 'MAPR'
    static constexpr uint32_t MAP_DATA = 0x4D415044;    // 'MAPD'
    static constexpr uint32_t MAP_POINTS = 0x4D415050;  // 'MAPP'
    static constexpr uint32_t TRCK = 0x5452434B;  // 'TRCK' - Track data header
    static constexpr uint32_t TCHU = 0x54434855;  // 'TCHU' - Track chunk
    static constexpr uint32_t RACE = 0x52414345;  // 'RACE' - Race data
    static constexpr uint32_t VSTA = 0x56535441;  // 'VSTA' - processed vehicle state
    static constexpr uint32_t TRK2 = 0x54524B32;  // 'TRK2' - dual-edge track (left + right polylines)
}

// Track rendering constants
namespace TrackConstants {
    static constexpr float TRACK_CORNER_RADIUS = 0.075f;
    static constexpr float TRACK_BORDER_WIDTH = 0.10f;
    static constexpr float TRACK_ASPHALT_WIDTH = 0.075f;
    static constexpr int TRACK_CORNER_SEGMENTS = 10;
}

// Vehicle simulation constants
namespace SimulationConstants {
    static constexpr float DEFAULT_DURATION_SECONDS = 60.0f;
    static constexpr float UPDATE_RATE_HZ = 60.0f;
    static constexpr float UPDATE_INTERVAL_MS = 1000.0f / UPDATE_RATE_HZ;
    static constexpr double METERS_PER_DEGREE_LAT = 111320.0;
    static constexpr double MIN_SPEED_KPH = 50.0;
    static constexpr double SPEED_VARIATION_KPH = 30.0;
    static constexpr double TWO_PI = 6.28318530718;
    static constexpr int PROGRESS_LOG_INTERVAL = 30;
}

// Race timing constants
namespace RaceConstants {
    // ✅ Настраиваемая нумерация кругов:
    // 0 = после первого пересечения старта машина едет Lap 0 → завершает → начинается Lap 1
    // 1 = после первого пересечения старта машина едет Lap 1 → завершает → начинается Lap 2
    static constexpr int LAP_START_NUMBER = 0;  // Рекомендуется: 1 (как в реальных гонках)
    
    // ✅ Lap Timer Delta Comparison Mode:
    // -1 = сравнение с лучшим кругом (best lap)
    // 0 = сравнение с предыдущим кругом (previous lap)
    // N > 0 = сравнение с конкретным кругом номер N
    static constexpr int LAP_DELTA_COMPARE_MODE = -1;  // По умолчанию: сравнение с лучшим
}

// Console colors
namespace ConsoleColors {
   static constexpr int CONSOLE_COLOR_GREEN = 10;
   static constexpr int CONSOLE_COLOR_YELLOW = 14;
   static constexpr int CONSOLE_COLOR_RED = 12;
   static constexpr int CONSOLE_DEFAULT = 7;
}


// Binary dual-edge track file format (.trk2)
// Layout: Trk2FileHeader | left_count * vec2 | right_count * vec2
#pragma pack(push, 1)
struct Trk2FileHeader {
    char     magic[4];          // 'T','R','K','2'
    uint32_t version;           // 2
    double   origin_easting;
    double   origin_northing;
    int32_t  origin_zone;
    char     origin_zone_char;
    uint8_t  pad[3];
    double   map_size;
    uint32_t left_count;
    uint32_t right_count;
};
#pragma pack(pop)

// Grid rendering constants
namespace GridConstants {
   static constexpr float GRID_CELL_SIZE = 20.0f;  // 20 meters per cell
   static constexpr float GRID_LINE_ALPHA = 0.12f;  // 12% visibility
   static constexpr float GRID_COLOR_R = 225.0f / 30.0f;     // White
   static constexpr float GRID_COLOR_G = 225.0f / 30.0f;
   static constexpr float GRID_COLOR_B = 225.0f / 30.0f;
}



