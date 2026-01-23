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
    static constexpr int VEHICLE_TIMEOUT_MS = 30000;         // 30 seconds
}

// Camera constants
namespace CameraConstants {
    static constexpr float CAMERA_MOVE_SPEED = 0.001f;
    static constexpr float CAMERA_ZOOM_MIN = 0.1f;
    static constexpr float CAMERA_ZOOM_MAX = 10.0f;
    static constexpr float CAMERA_FRICTION = 0.85f;
}

// Network constants
namespace NetworkConstants {
    static constexpr int DEFAULT_SERVER_PORT = 777;
    static constexpr const char* DEFAULT_SERVER_IP = "136.169.18.31";
}

// Track rendering constants
namespace TrackConstants {
    static constexpr float TRACK_CORNER_RADIUS = 0.075f;
    static constexpr float TRACK_BORDER_WIDTH = 0.085f;
    static constexpr float TRACK_ASPHALT_WIDTH = 0.075f;
    static constexpr int TRACK_CORNER_SEGMENTS = 10;
}
