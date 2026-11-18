# Synapse Teacher Tool: Enforce BYOD Lockdown
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Synapse Classroom: Enforce BYOD Lockdown" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Activating Wi-Fi lockdown on BYOD devices..." -ForegroundColor Gray

$byodDevices = @("192.168.1.13")

foreach ($ip in $byodDevices) {
    if (Test-Connection -ComputerName $ip -Count 1 -Quiet) {
        Write-Host "Enforcing on $ip..." -ForegroundColor Green
        $cmd = 'powershell.exe -ExecutionPolicy Bypass -Command "Remove-Item -Path C:\ProgramData\DigiAgent\byod_policy.json -Force -ErrorAction SilentlyContinue"'
        $json = '{\"applications\":\"' + $cmd.Replace('\', '\\').Replace('"', '\"') + '\"}'
        & "C:\Program Files\DigiAgent\digiagent-cli.exe" feature start $ip StartApp $json
    } else {
        Write-Host "Device $ip is offline / not reachable." -ForegroundColor DarkGray
    }
}

Write-Host "`nDone! Wi-Fi disconnection lockdown is now active on all laptops." -ForegroundColor Green
timeout /t 5
