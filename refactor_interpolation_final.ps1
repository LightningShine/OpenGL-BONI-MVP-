$ErrorActionPreference = "Stop"

Write-Host "Final Interpolation.cpp polish..." -ForegroundColor Green

$file = "OpenGL\src\rendering\Interpolation.cpp"

if (Test-Path $file) {
    Write-Host "Processing $file..." -ForegroundColor Cyan
    $content = Get-Content $file -Raw -Encoding UTF8
    
    $replacements = @{
        # Local variables (snake_case)
        "\bmaxDist\b" = "max_distance"
        "\bisClosed\b" = "is_closed"
        "\bstartPoint\b" = "start_point"
        "\bendPoint\b" = "end_point"
        "\bactualRadius\b" = "actual_radius"
        "\bcontrolPoint\b" = "control_point"
        "\btangentDirection\b" = "tangent_direction"
    }
    
    foreach ($old in $replacements.Keys) {
        $new = $replacements[$old]
        $content = $content -replace $old, $new
    }
    
    $content | Out-File -FilePath $file -Encoding UTF8 -NoNewline
    Write-Host "  Done!" -ForegroundColor Green
} else {
    Write-Host "File not found: $file" -ForegroundColor Red
}

Write-Host "Final polish done!" -ForegroundColor Green
