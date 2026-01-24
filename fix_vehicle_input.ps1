$ErrorActionPreference = "Stop"

Write-Host "Fixing Vehicle.cpp and UI.cpp..." -ForegroundColor Green

$files = @(
    "OpenGL\src\vehicle\Vehicle.cpp",
    "OpenGL\UI.cpp"
)

$replacements = @{
    "\bm_MapLoaded\b" = "g_is_map_loaded"
    "\bmapOrigin\.originDD_lat\b" = "g_map_origin.m_origin_lat_dd"
    "\bmapOrigin\.originDD_lon\b" = "g_map_origin.m_origin_lon_dd"
    "\bmapOrigin\b" = "g_map_origin"
    "\bDDToMetr\b" = "coordinatesToMeters"
    "\bCordinateDifirenceFromOrigin\b" = "getCoordinateDifferenceFromOrigin"
    "\bLoadTrackFromData\b" = "loadTrackFromData"
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

Write-Host "Fix complete!" -ForegroundColor Green
