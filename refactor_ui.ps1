$ErrorActionPreference = "Stop"

Write-Host "Starting UI refactoring..." -ForegroundColor Green

$files = @(
    "OpenGL\UI.h",
    "OpenGL\UI.cpp"
)

$replacements = @{
    # Methods already look good (PascalCase -> should be camelCase for consistency)
    # But UI class methods can stay as they are since it's a public API
    # We'll just fix internal variables and parameters
    
    # Private member variables already have m_ prefix - good!
    # Let's fix any local variables and parameters
    
    # Parameters (snake_case)
    "\btexWidth\b" = "tex_width"
    "\btexHeight\b" = "tex_height"
    "\bimageData\b" = "image_data"
    "\bfilePath\b" = "file_path"
    "\bfilename\b" = "filename"
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

Write-Host "UI refactoring complete!" -ForegroundColor Green
