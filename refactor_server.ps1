$ErrorActionPreference = "Stop"

Write-Host "Starting Server refactoring..." -ForegroundColor Green

$files = @(
    "OpenGL\src\network\Server.h",
    "OpenGL\src\network\Server.cpp",
    "OpenGL\src\core\main.cpp"
)

$replacements = @{
    # Global variables (g_ + snake_case + is_ for bool)
    "\bServerIsRunning_b\b" = "g_is_server_running"
    "\bServerShouldClose_b\b" = "g_should_close_server"
    
    # Functions (camelCase)
    "ServerWork" = "serverWork"
    "ServerStop" = "serverStop"
    "ServerRunningStatus" = "isServerRunning"
    "ChangeServerRunningStatus" = "toggleServerRunning"
    "ContinueServerRunning" = "continueServerRunning"
    "OnSteamNetConnectionStatusChanged" = "onSteamNetConnectionStatusChanged"
    "PollIncomingMessages" = "pollIncomingMessages"
    "PollConnectionStateChanges" = "pollConnectionStateChanges"
    "PollLocalUserInput" = "pollLocalUserInput"
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

Write-Host "Server refactoring complete!" -ForegroundColor Green
