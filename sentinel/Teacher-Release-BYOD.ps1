# Synapse Teacher Tool: Release BYOD Laptops
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Synapse Classroom: Release BYOD Laptops" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Disabling Wi-Fi lockdown on BYOD devices..." -ForegroundColor Gray

$byodDevices = @("192.168.1.13")

foreach ($ip in $byodDevices) {
    if (Test-Connection -ComputerName $ip -Count 1 -Quiet) {
        Write-Host "Releasing $ip..." -ForegroundColor Green
        $cmd = 'powershell.exe -ExecutionPolicy Bypass -Command "Set-Content -Path C:\ProgramData\DigiAgent\byod_policy.json -Value ''{\"\"released\"\": true}'' -Force"'
        $json = '{\"applications\":\"' + $cmd.Replace('\', '\\').Replace('"', '\"') + '\"}'
        & "C:\Program Files\DigiAgent\digiagent-cli.exe" feature start $ip StartApp $json
    } else {
        Write-Host "Device $ip is offline / not reachable." -ForegroundColor DarkGray
    }
}

Write-Host "`nDone! Students can now disconnect from Wi-Fi without locking." -ForegroundColor Green
timeout /t 5
