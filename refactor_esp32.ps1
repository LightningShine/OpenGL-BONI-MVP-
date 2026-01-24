$ErrorActionPreference = "Stop"

Write-Host "Starting ESP32_Code refactoring..." -ForegroundColor Green

$files = @(
    "OpenGL\src\network\ESP32_Code.h",
    "OpenGL\src\network\ESP32_Code.cpp"
)

$replacements = @{
    # Functions (camelCase)
    "Test_Serial" = "testSerial"
    "OpenCOMPort" = "openCOMPort"
    "ReadingFromCOM" = "readFromCOM"
    
    # Global/local variables (snake_case)
    "\bportName\b" = "port_name"
    "\bpIncomingMsg\b" = "incoming_messages"
    "\bnumMsgs\b" = "message_count"
    "\bpData\b" = "packet_data"
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

Write-Host "ESP32_Code refactoring complete!" -ForegroundColor Green
