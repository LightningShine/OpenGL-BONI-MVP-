$ErrorActionPreference = "Stop"

# Refactoring script for Vehicle class
Write-Host "Starting Vehicle refactoring..." -ForegroundColor Green

$vehicleH = "OpenGL\src\vehicle\Vehicle.h"
$vehicleCpp = "OpenGL\src\vehicle\Vehicle.cpp"

# Read files
$contentH = Get-Content $vehicleH -Raw -Encoding UTF8
$contentCpp = Get-Content $vehicleCpp -Raw -Encoding UTF8

# Vehicle class fields
$replacements = @{
    # Class name
    "Venchile" = "Vehicle"
    
    # Member variables
    "v_latDD" = "m_lat_dd"
    "v_lonDD" = "m_lon_dd"
    "v_metr_easting" = "m_meters_easting"
    "v_metr_north" = "m_meters_northing"
    "v_norX" = "m_normalized_x"
    "v_norY" = "m_normalized_y"
    "v_speedKPH" = "m_speed_kph"
    "v_acceleration" = "m_acceleration"
    "v_gForceX" = "m_g_force_x"
    "v_gForceY" = "m_g_force_y"
    "v_fixtype" = "m_fix_type"
    "v_ID" = "m_id"
    "\.name\b" = ".m_name"
    "lastUpdateTime" = "m_last_update_time"
    "cachedColor" = "m_cached_color"
    
    # Methods
    "GetColor" = "getColor"
    
    # Global variables
    "g_Vehicles\b" = "g_vehicles"
    "g_VehiclesMutex" = "g_vehicles_mutex"
    "g_VehiclesActive" = "g_is_vehicles_active"
    
    # Functions
    "GenerateVehicleID" = "generateVehicleID"
    "VehicleLoop" = "vehicleLoop"
    "VehicleClose" = "vehicleClose"
    "GenerateCircle" = "generateCircle"
    "RenderVehicle\b" = "renderVehicle"
    "RenderAllVehicles" = "renderAllVehicles"
    
    # Parameters
    "shaderProgram" = "shader_program"
    "\bVAO\b" = "vao"
    "\bVBO\b" = "vbo"
    "cameraPos" = "camera_pos"
    "cameraZoom" = "camera_zoom"
}

foreach ($old in $replacements.Keys) {
    $new = $replacements[$old]
    $contentH = $contentH -replace $old, $new
    $contentCpp = $contentCpp -replace $old, $new
}

# Write back
$contentH | Out-File -FilePath $vehicleH -Encoding UTF8 -NoNewline
$contentCpp | Out-File -FilePath $vehicleCpp -Encoding UTF8 -NoNewline

Write-Host "Vehicle refactoring complete!" -ForegroundColor Green
Write-Host "Modified files: Vehicle.h, Vehicle.cpp" -ForegroundColor Yellow
