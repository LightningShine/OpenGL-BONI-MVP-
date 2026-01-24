$ErrorActionPreference = "Stop"

Write-Host "Starting full project refactoring..." -ForegroundColor Green

# Files to refactor
$files = @(
    "OpenGL\src\core\main.cpp",
    "OpenGL\src\input\Input.h",
    "OpenGL\src\input\Input.cpp",
    "OpenGL\src\network\ESP32_Code.h",
    "OpenGL\src\network\ESP32_Code.cpp",
    "OpenGL\Client.cpp",
    "OpenGL\src\network\Server.cpp",
    "OpenGL\src\network\Server.h"
)

# Replacements
$replacements = @{
    "Venchile" = "Vehicle"
    "\.v_latDD\b" = ".m_lat_dd"
    "\.v_lonDD\b" = ".m_lon_dd"
    "\.v_metr_easting\b" = ".m_meters_easting"
    "\.v_metr_north\b" = ".m_meters_northing"
    "\.v_norX\b" = ".m_normalized_x"
    "\.v_norY\b" = ".m_normalized_y"
    "\.v_speedKPH\b" = ".m_speed_kph"
    "\.v_acceleration\b" = ".m_acceleration"
    "\.v_gForceX\b" = ".m_g_force_x"
    "\.v_gForceY\b" = ".m_g_force_y"
    "\.v_fixtype\b" = ".m_fix_type"
    "\.v_ID\b" = ".m_id"
    "\.name\b" = ".m_name"
    "\.lastUpdateTime\b" = ".m_last_update_time"
    "\.cachedColor\b" = ".m_cached_color"
    "GetColor" = "getColor"
    "g_Vehicles\b" = "g_vehicles"
    "g_VehiclesMutex\b" = "g_vehicles_mutex"
    "g_VehiclesActive\b" = "g_is_vehicles_active"
    "GenerateVehicleID" = "generateVehicleID"
    "VehicleLoop" = "vehicleLoop"
    "VehicleClose" = "vehicleClose"
    "GenerateCircle" = "generateCircle"
    "RenderVehicle\b" = "renderVehicle"
    "RenderAllVehicles" = "renderAllVehicles"
    "shaderProgram" = "shader_program"
    "\bVAO\b" = "vao"
    "\bVBO\b" = "vbo"
    "\bcameraPos\b" = "camera_pos"
    "\bcameraZoom\b" = "camera_zoom"
    "\bcameraPosition\b" = "camera_position"
    "\bcameraMoveSpeed\b" = "camera_move_speed"
    "\bcameraVelocity\b" = "camera_velocity"
    "\bmapBoundX\b" = "map_bound_x"
    "\bmapBoundY\b" = "map_bound_y"
    "\bpointsMutex\b" = "points_mutex"
}

foreach ($file in $files) {
    if (Test-Path $file) {
        Write-Host "Processing $file..." -ForegroundColor Cyan
        $content = Get-Content $file -Raw -Encoding UTF8
        
        foreach ($old in $replacements.Keys) {
            $new = $replacements[$old]
            $content = $content -replace $old, $new
        }
        
        $content | Out-File -FilePath $file -Encoding UTF8 -NoNewline
        Write-Host "  Done!" -ForegroundColor Green
    }
}

Write-Host "`nRefactoring complete!" -ForegroundColor Green
