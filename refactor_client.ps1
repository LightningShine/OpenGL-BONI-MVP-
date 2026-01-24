$ErrorActionPreference = "Stop"

Write-Host "Starting Client refactoring..." -ForegroundColor Green

$files = @(
    "OpenGL\src\network\Client.h",
    "OpenGL\Client.cpp",
    "OpenGL\src\core\main.cpp"
)

$replacements = @{
    # Global variables (g_ + snake_case + is_/should_ for bool)
    "\bClientIsRunning_b\b" = "g_is_client_running"
    "\bClientShouldClose_b\b" = "g_should_close_client"
    "\bg_hConnection\b" = "g_connection_handle"
    
    # Functions (camelCase)
    "OnClientConnectionStatusChanged" = "onClientConnectionStatusChanged"
    "ConnectToServer" = "connectToServer"
    "SendClientMessage" = "sendClientMessage"
    "ListenMessagesFromServer" = "listenMessagesFromServer"
    "ClientRunningStatus" = "isClientRunning"
    "ChangeClientRunningStatus" = "toggleClientRunning"
    "ClientStop" = "clientStop"
    "ContinueClientRunning" = "continueClientRunning"
    "ClientStart" = "clientStart"
    
    # Parameters and local variables
    "\bserverAddr\b" = "server_address"
    "\bserverName\b" = "server_name"
    "\bipPortPattern\b" = "ip_port_pattern"
    "\berrMsg\b" = "error_message"
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

Write-Host "Client refactoring complete!" -ForegroundColor Green
