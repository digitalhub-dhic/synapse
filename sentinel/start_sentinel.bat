@echo off
title Synapse AI Sentinel Launcher
cd /d "C:\SynapseAISentinel"

echo ========================================================
echo         SYNAPSE AI SENTINEL - ENTERPRISE GUARDIAN
echo ========================================================
set "SENTINEL_SECURITY_DIR=%LOCALAPPDATA%\SynapseSentinel\security"
if not exist "C:\SynapseAISentinel\Set-SentinelConsolePassword.ps1" (
    echo [ERROR] Sentinel password setup script is missing.
    exit /b 1
)
if not exist "%SENTINEL_SECURITY_DIR%\console_credential.json" (
    echo First run: create the teacher-console password for this Windows account.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\SynapseAISentinel\Set-SentinelConsolePassword.ps1"
    if errorlevel 1 exit /b 1
)
if not exist "%SENTINEL_SECURITY_DIR%\web_session.key" (
    echo Session security key is missing. Reconfigure Sentinel sign-in.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\SynapseAISentinel\Set-SentinelConsolePassword.ps1"
    if errorlevel 1 exit /b 1
)

echo Restarting the Sentinel daemon with the current security settings...
powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process ^| Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -like '*sentinel_daemon.py*' } ^| ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" >nul 2>&1
timeout /t 1 /nobreak >nul
start "" /b "C:\Program Files\Python313\pythonw.exe" "C:\SynapseAISentinel\sentinel_daemon.py"
timeout /t 5 /nobreak > nul
set "SENTINEL_PROCESS_COUNT=0"
for /f %%C in ('powershell.exe -NoProfile -Command "@(Get-CimInstance Win32_Process ^| Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -like '*sentinel_daemon.py*' }).Count"') do set "SENTINEL_PROCESS_COUNT=%%C"
if "%SENTINEL_PROCESS_COUNT%"=="0" (
    echo [ERROR] Sentinel did not stay running. Check the Sentinel logs and Python installation.
    exit /b 1
)

echo Opening Teacher Sentinel Control Dashboard...
start http://127.0.0.1:5050
echo Sentinel active! You can close this window.
timeout /t 3 > nul
exit
