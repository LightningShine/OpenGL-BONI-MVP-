$ErrorActionPreference = "Stop"

Write-Host "Starting Input refactoring..." -ForegroundColor Green

$files = @(
    "OpenGL\src\input\Input.h",
    "OpenGL\src\input\Input.cpp",
    "OpenGL\src\core\main.cpp"
)

$replacements = @{
    # Global variables (????????? g_ ??????? ? snake_case)
    "\bmapOrigin\b" = "g_map_origin"
    "\bm_MapLoaded\b" = "g_is_map_loaded"
    
    # Class name
    "\bMapDate\b" = "MapOrigin"
    
    # Functions (camelCase)
    "InputOrigin" = "inputOrigin"
    "InputDataOpenGL" = "inputDataOpenGL"
    "CordinatesToDecimalFormat" = "coordinatesToDecimalFormat"
    "CreateOriginDD" = "createOriginDD"
    "DDToMetr" = "coordinatesToMeters"
    "CordinateDifirenceFromOrigin" = "getCoordinateDifferenceFromOrigin"
    "InputDatainCode" = "inputDataInCode"
    "ChoseInputMode" = "chooseInputMode"
    "LoadTrackFromData" = "loadTrackFromData"
    
    # Class fields (m_ prefix + snake_case)
    "\.originDD_lat\b" = ".m_origin_lat_dd"
    "\.originDD_lon\b" = ".m_origin_lon_dd"
    "\.originMetr_est\b" = ".m_origin_meters_easting"
    "\.originMetr_nort\b" = ".m_origin_meters_northing"
    "\.originZone_int\b" = ".m_origin_zone_int"
    "\.origin_mapsize\b" = ".m_map_size"
    "\.originZone_char\b" = ".m_origin_zone_char"
    
    # Parameters and local variables (snake_case)
    "\bdec_lat_deg\b" = "decimal_lat_deg"
    "dec_lon_deg\b" = "decimal_lon_deg"
    "\beasting\b" = "easting_meters"
    "\bnorthing\b" = "northing_meters"
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

Write-Host "Input refactoring complete!" -ForegroundColor Green
