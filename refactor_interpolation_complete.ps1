$ErrorActionPreference = "Stop"

Write-Host "Complete Interpolation.cpp refactoring..." -ForegroundColor Green

$file = "OpenGL\src\rendering\Interpolation.cpp"

if (Test-Path $file) {
    Write-Host "Processing $file..." -ForegroundColor Cyan
    $content = Get-Content $file -Raw -Encoding UTF8
    
    $replacements = @{
        # Local variables (snake_case)
        "\btriangleStripPoints\b" = "triangle_strip_points"
        "\bhalfWidth\b" = "half_width"
        "\bprevNormal\b" = "prev_normal"
        "\bleftPoint\b" = "left_point"
        "\brightPoint\b" = "right_point"
        "\blastSP\b" = "last_point"
        "\bsmoothed\b" = "smoothed_points"
        "\bavg\b" = "average"
        "\blineStart\b" = "line_start"
        "\blineEnd\b" = "line_end"
        "\bmag\b" = "magnitude"
        "\bdMax\b" = "max_distance"
        "\bindex\b" = "point_index"
        "\bleftSide\b" = "left_side"
        "\brightSide\b" = "right_side"
        "\bresultList\b" = "result_points"
        
        # Static helper functions (camelCase)
        "\bPerpendicularDistance\b" = "perpendicularDistance"
        "\bDouglasPeuckerRecursive\b" = "douglasPeuckerRecursive"
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

Write-Host "Complete refactoring done!" -ForegroundColor Green
