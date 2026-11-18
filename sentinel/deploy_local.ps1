Stop-Service -Name "DigiAgentService" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "digiagent-master" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "digiagent-server" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "digiagent-worker" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "digiagent-service" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$src = "C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build-output"
$dst = "C:\Program Files\DigiAgent"

Copy-Item "$src\master\veyon-master.exe" "$dst\digiagent-master.exe" -Force
Copy-Item "$src\server\veyon-server.exe" "$dst\digiagent-server.exe" -Force
Copy-Item "$src\worker\veyon-worker.exe" "$dst\digiagent-worker.exe" -Force
Copy-Item "$src\service\veyon-service.exe" "$dst\digiagent-service.exe" -Force
Copy-Item "$src\cli\veyon-cli.exe" "$dst\digiagent-cli.exe" -Force
Copy-Item "$src\configurator\veyon-configurator.exe" "$dst\digiagent-configurator.exe" -Force

# Also copy to Auto-Updater Payload
$payload = "\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload"
if (Test-Path $payload) {
    Copy-Item "$src\master\veyon-master.exe" "$payload\digiagent-master.exe" -Force -ErrorAction SilentlyContinue
    Copy-Item "$src\server\veyon-server.exe" "$payload\digiagent-server.exe" -Force -ErrorAction SilentlyContinue
    Copy-Item "$src\worker\veyon-worker.exe" "$payload\digiagent-worker.exe" -Force -ErrorAction SilentlyContinue
    Copy-Item "$src\service\veyon-service.exe" "$payload\digiagent-service.exe" -Force -ErrorAction SilentlyContinue
    Copy-Item "$src\cli\veyon-cli.exe" "$payload\digiagent-cli.exe" -Force -ErrorAction SilentlyContinue
}

Start-Sleep -Seconds 1
Start-Service -Name "DigiAgentService" -ErrorAction SilentlyContinue
Start-Process -FilePath "$dst\digiagent-master.exe" -WorkingDirectory $dst
Start-Sleep -Seconds 2

Get-Process -Name "digiagent-master", "digiagent-server" -ErrorAction SilentlyContinue | Select-Object Id, ProcessName, StartTime
Write-Host "Local deployment and master restart complete!"
