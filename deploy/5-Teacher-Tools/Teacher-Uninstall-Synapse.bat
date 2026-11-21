@echo off
title Synapse Teacher Master Uninstaller
color 0F
cls
echo ========================================================
echo       SYNAPSE CLASSROOM CLIENT - INSTRUCTOR UNINSTALL
echo ========================================================
echo.
echo [!] This tool requires the Instructor Master Password.
echo.

set /p PWD="Enter Instructor Password: "
powershell -NoProfile -ExecutionPolicy Bypass -Command "$h = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new([System.Text.Encoding]::UTF8.GetBytes('SynapseEnterpriseSalt2026' + '%PWD%')))).Hash.ToLower(); if ($h -eq 'efa0f57bdc9955b979a271903c7ae1c79272c1298df1c17d10c929c30860c33d') { exit 0 } else { exit 1 }" >nul 2>&1
if %errorlevel% neq 0 (
    color 4F
    echo.
    echo [ERROR] Access Denied: Incorrect Master Password.
    echo Unauthorized uninstallation is prohibited.
    pause
    exit /b 1
)

color 2F
echo.
echo [OK] Password Verified. Removing Synapse Client...
echo.

:: 1. Terminate Watchdog and Scheduled Task
schtasks /End /TN "SynapseWatchdog" >nul 2>&1
schtasks /Delete /TN "SynapseWatchdog" /F >nul 2>&1
taskkill /F /IM SynapseWatchdog.exe >nul 2>&1
taskkill /F /IM SynapseBYODGuard.exe >nul 2>&1

:: 2. Stop and Remove DigiAgent Service
sc config DigiAgentService start= disabled >nul 2>&1
net stop DigiAgentService >nul 2>&1
taskkill /F /IM digiagent-server.exe >nul 2>&1
taskkill /F /IM digiagent-worker.exe >nul 2>&1
taskkill /F /IM digiagent-service.exe >nul 2>&1
sc delete DigiAgentService >nul 2>&1

:: 3. Clean files and registries
if exist "C:\Program Files\DigiAgent" (
    rmdir /S /Q "C:\Program Files\DigiAgent" >nul 2>&1
)
if exist "C:\ProgramData\DigiAgent" (
    rmdir /S /Q "C:\ProgramData\DigiAgent" >nul 2>&1
)

reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "SynapseBYODGuard" /f >nul 2>&1
reg delete "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "SynapseBYODGuard" /f >nul 2>&1

echo [SUCCESS] Synapse Classroom Client has been cleanly removed.
echo The laptop is now returned to unrestricted personal mode.
pause
