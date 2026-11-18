$ErrorActionPreference = "SilentlyContinue"
Set-Location "C:\SynapseAISentinel"

# Stop any old instances
Get-Process -Name python,pythonw -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*Python313*" } | Stop-Process -Force

# Start sentinel_daemon.py in current interactive session
Start-Process -FilePath "C:\Program Files\Python313\pythonw.exe" -ArgumentList "C:\SynapseAISentinel\sentinel_daemon.py" -WorkingDirectory "C:\SynapseAISentinel" -WindowStyle Hidden
