$ErrorActionPreference = "Stop"
Set-Location "C:\SynapseAISentinel"

$securityDir = Join-Path $env:LOCALAPPDATA 'SynapseSentinel\security'
if (-not (Test-Path (Join-Path $securityDir 'console_credential.json')) -or
    -not (Test-Path (Join-Path $securityDir 'web_session.key'))) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\SynapseAISentinel\Set-SentinelConsolePassword.ps1'
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

# Stop only a prior Sentinel daemon. Do not terminate unrelated Python apps.
Get-CimInstance Win32_Process |
    Where-Object { $_.Name -in @('python.exe', 'pythonw.exe') -and $_.CommandLine -like '*sentinel_daemon.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 500

# Start sentinel_daemon.py in current interactive session
$daemon = Start-Process -FilePath "C:\Program Files\Python313\pythonw.exe" -ArgumentList "C:\SynapseAISentinel\sentinel_daemon.py" -WorkingDirectory "C:\SynapseAISentinel" -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 1
if ($daemon.HasExited) { throw 'Sentinel daemon exited during startup.' }
