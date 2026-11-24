@echo off
title Synapse Teacher Master Uninstaller
color 0F
cls
echo ========================================================
echo       SYNAPSE CLASSROOM CLIENT - INSTRUCTOR UNINSTALL
echo ========================================================
echo.
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] Requesting Administrator Privileges...
    powershell.exe -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
if not exist "%~dp0Verify-SynapseTeacherPassword.ps1" (
    echo [ERROR] Per-device teacher password verifier is missing.
    pause
    exit /b 1
)
echo [!] This tool requires this device's teacher override password.
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Verify-SynapseTeacherPassword.ps1"
if %errorlevel% neq 0 (
    color 4F
    echo.
    echo [ERROR] Access denied. The Synapse client was not removed.
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

:: 3. Restore administrator access on older installs before removing files.
for %%D in ("C:\Program Files\DigiAgent" "C:\ProgramData\DigiAgent") do (
    if exist "%%~D" (
        icacls "%%~D" /T /C /remove:d Users /grant:r "SYSTEM:(OI)(CI)(F)" "Administrators:(OI)(CI)(F)" >nul 2>&1
        if errorlevel 1 (
            echo [ERROR] Could not restore administrator access to %%~D.
            pause
            exit /b 1
        )
    )
)

:: 4. Clean files and registries
if exist "C:\Program Files\DigiAgent" (
    rmdir /S /Q "C:\Program Files\DigiAgent" >nul 2>&1
)
if exist "C:\ProgramData\DigiAgent" (
    rmdir /S /Q "C:\ProgramData\DigiAgent" >nul 2>&1
)
if exist "C:\Program Files\DigiAgent" (
    echo [ERROR] Synapse files remain. Complete removal from Windows Apps and Features.
    pause
    exit /b 1
)
if exist "C:\ProgramData\DigiAgent" (
    echo [ERROR] Synapse data remains; some files may still be in use.
    pause
    exit /b 1
)

reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "SynapseBYODGuard" /f >nul 2>&1
reg delete "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "SynapseBYODGuard" /f >nul 2>&1

echo [SUCCESS] Synapse components have been removed.
echo Tailscale remains installed and visible; remove it separately only if it is not used by other applications.
pause
