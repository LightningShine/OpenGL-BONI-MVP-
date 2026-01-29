#pragma once

// Map and coordinate system constants
namespace MapConstants {
    static constexpr double MAP_SIZE = 100.0; // 100 meters = 1.0 in OpenGL coordinates
    static constexpr float MAP_BOUND_X = 2.0f;
    static constexpr float MAP_BOUND_Y = 2.0f;
}

// Vehicle rendering constants
namespace VehicleConstants {
    static constexpr float VEHICLE_BODY_RADIUS = 0.05f;      // 5 meters
    static constexpr float VEHICLE_OUTLINE_RADIUS = 0.055f;  // White outline
    static constexpr int VEHICLE_CIRCLE_SEGMENTS = 20;
    static constexpr int VEHICLE_TIMEOUT_MS = 300;         // 30 seconds
}

// Camera constants
namespace CameraConstants {
    static constexpr float CAMERA_MOVE_SPEED = 0.01f;
    static constexpr float CAMERA_ZOOM_MIN = 0.1f;
    static constexpr float CAMERA_ZOOM_MAX = 10.0f;
    static constexpr float CAMERA_FRICTION = 0.85f;
}

// Network constants
namespace NetworkConstants {
    static constexpr int DEFAULT_SERVER_PORT = 777;
    static constexpr const char* DEFAULT_SERVER_IP = "136.169.18.31";
    static constexpr const char* DEFAULT_LOCAL_SERVER_IP = "127.0.0.1";
    static constexpr const char* SERVER_PASSWORD = "mypassword123";  
    static constexpr int MAX_AUTH_ATTEMPTS = 10;
}

namespace PacketMagic {
    static constexpr uint32_t AUTH = 0x41555448;  // 'AUTH'
    static constexpr uint32_t RESP = 0x52455350;  // 'RESP'
    static constexpr uint32_t DATA = 0x44415441;  // 'DATA'
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
    static constexpr float UPDATE_RATE_HZ = 30.0f;
    static constexpr float UPDATE_INTERVAL_MS = 1000.0f / UPDATE_RATE_HZ;
    static constexpr double METERS_PER_DEGREE_LAT = 111320.0;
    static constexpr double MIN_SPEED_KPH = 50.0;
    static constexpr double SPEED_VARIATION_KPH = 30.0;
    static constexpr double TWO_PI = 6.28318530718;
    static constexpr int PROGRESS_LOG_INTERVAL = 30;
}

// Console colors
namespace ConsoleColors {
   static constexpr int CONSOLE_COLOR_GREEN = 10;
   static constexpr int CONSOLE_COLOR_YELLOW = 14;
   static constexpr int CONSOLE_COLOR_RED = 12;
   static constexpr int CONSOLE_DEFAULT = 7;
}


// Grid rendering constants
namespace GridConstants {
   static constexpr float GRID_CELL_SIZE = 20.0f;  // 20 meters per cell
   static constexpr float GRID_LINE_ALPHA = 0.12f;  // 12% visibility
   static constexpr float GRID_COLOR_R = 225.0f / 30.0f;     // White
   static constexpr float GRID_COLOR_G = 225.0f / 30.0f;
   static constexpr float GRID_COLOR_B = 225.0f / 30.0f;
}



