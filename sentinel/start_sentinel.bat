@echo off
title Synapse AI Sentinel Launcher
cd /d "C:\SynapseAISentinel"

echo ========================================================
echo         SYNAPSE AI SENTINEL - ENTERPRISE GUARDIAN
echo ========================================================
echo Checking if Sentinel Daemon is already running...

powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*sentinel_daemon.py*' }" > nul 2>&1
if %errorlevel% equ 0 (
    echo Sentinel is already running in background!
) else (
    echo Starting Sentinel AI Engine...
    start "" /b "C:\Program Files\Python313\pythonw.exe" "C:\SynapseAISentinel\sentinel_daemon.py"
    timeout /t 2 /nobreak > nul
)

echo Opening Teacher Sentinel Control Dashboard...
start http://127.0.0.1:5050
echo Sentinel active! You can close this window.
timeout /t 3 > nul
exit
