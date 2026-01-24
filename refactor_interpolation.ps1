$ErrorActionPreference = "Stop"

Write-Host "Starting Interpolation refactoring..." -ForegroundColor Green

$files = @(
    "OpenGL\src\rendering\Interpolation.h",
    "OpenGL\src\rendering\Interpolation.cpp",
    "OpenGL\src\core\main.cpp"
)

$replacements = @{
    # Functions (camelCase with verbs)
    "\bLerpDerivative\b" = "lerpDerivative"
    "\bInterpolatePointsWithTangents\b" = "interpolatePointsWithTangents"
    "\bInterpolateRoundedPolyline\b" = "interpolateRoundedPolyline"
    "\bSmoothPath\b" = "smoothPath"
    "\bSimplifyPath\b" = "simplifyPath"
    "\bFilterPointsByDistance\b" = "filterPointsByDistance"
    "\bGenerateTriangleStripFromLine\b" = "generateTriangleStripFromLine"
    
    # Parameters and local variables (snake_case)
    "\boriginalPoints\b" = "original_points"
    "\bpointsPerSegment\b" = "points_per_segment"
    "\bsegmentsPerCorner\b" = "segments_per_corner"
    "\brawPoints\b" = "raw_points"
    "\bwindowSize\b" = "window_size"
    "\bminDistance\b" = "min_distance"
    "\bsplinePoints\b" = "spline_points"
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

Write-Host "Interpolation refactoring complete!" -ForegroundColor Green
