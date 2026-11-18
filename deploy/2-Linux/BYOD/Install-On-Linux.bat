@echo off
:: ====================================================================
::   SYNAPSE COMMAND - LINUX BYOD INSTALLER LAUNCHER (.BAT)
::   Executes:
::     chmod +x install-synapse-byod.sh
::     sudo ./install-synapse-byod.sh
:: ====================================================================
title Synapse Command - Linux BYOD Installer Launcher
color 0B
setlocal enabledelayedexpansion

echo ====================================================================
echo        SYNAPSE COMMAND - LINUX BYOD INSTALLER LAUNCHER
echo ====================================================================
echo.
echo  Target Script: install-synapse-byod.sh
echo  Action:        chmod +x install-synapse-byod.sh ^&^& sudo ./install-synapse-byod.sh
echo.
echo  Choose how you want to run this installer:
echo.
echo    [1] Remote Linux Laptop over Network (Wi-Fi / LAN via SSH)
echo    [2] Local Linux Subsystem (WSL / Ubuntu on this PC)
echo    [3] View Instructions for Direct Execution on Linux
echo    [4] Exit
echo.
set /p mode="Select option [1-4] (Press Enter for 1): "
if "%mode%"=="" set mode=1

if "%mode%"=="1" goto REMOTE_SSH
if "%mode%"=="2" goto LOCAL_WSL
if "%mode%"=="3" goto SHOW_INSTRUCTIONS
if "%mode%"=="4" exit /b 0
goto REMOTE_SSH

:REMOTE_SSH
echo.
echo --------------------------------------------------------------------
echo  REMOTE LINUX DEPLOYMENT
echo --------------------------------------------------------------------
set /p target_ip=" Enter Linux Laptop IP Address: "
if "%target_ip%"=="" (
    echo [ERROR] IP address cannot be empty.
    pause
    exit /b 1
)

set /p target_user=" Enter Linux Username [default: student]: "
if "%target_user%"=="" set target_user=student

echo.
echo [*] Testing network reachability to %target_ip%...
ping -n 1 -w 1500 %target_ip% >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] Device %target_ip% did not respond to ping. Attempting connection anyway...
) else (
    echo [OK] Device is reachable on the network.
)

echo.
echo [*] Copying install-synapse-byod.sh to %target_user%@%target_ip%...
scp -o StrictHostKeyChecking=no "%~dp0install-synapse-byod.sh" %target_user%@%target_ip%:~/install-synapse-byod.sh
if %errorlevel% neq 0 (
    echo.
    echo [!] Failed to copy installer to remote Linux device.
    echo     Please verify:
    echo       1. Target IP (%target_ip%) and Username (%target_user%) are correct.
    echo       2. SSH service is running on Linux ('sudo systemctl start ssh').
    echo.
    pause
    exit /b 1
)

echo.
echo [*] Running: chmod +x ~/install-synapse-byod.sh ^&^& sudo ~/install-synapse-byod.sh
echo [*] (Enter the Linux user's sudo password when prompted)
echo --------------------------------------------------------------------
ssh -t -o StrictHostKeyChecking=no %target_user%@%target_ip% "chmod +x ~/install-synapse-byod.sh && sudo ~/install-synapse-byod.sh"

echo.
echo ====================================================================
echo  Installation execution finished!
echo  Synapse Command will auto-discover and show the device under BYOD.
echo ====================================================================
pause
exit /b 0

:LOCAL_WSL
echo.
echo --------------------------------------------------------------------
echo  LOCAL WSL / LINUX EXECUTION
echo --------------------------------------------------------------------
where wsl >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Windows Subsystem for Linux (WSL) is not installed on this PC.
    pause
    exit /b 1
)

echo [*] Launching in WSL with root privileges...
wsl -u root bash -c "cd '$(wslpath '%~dp0')' && chmod +x install-synapse-byod.sh && ./install-synapse-byod.sh"
echo.
echo [OK] WSL Execution complete.
pause
exit /b 0

:SHOW_INSTRUCTIONS
cls
echo ====================================================================
echo     DIRECT EXECUTION INSTRUCTIONS ON LINUX
echo ====================================================================
echo.
echo On the student's Linux laptop:
echo   1. Connect to classroom Wi-Fi.
echo   2. Open Terminal.
echo   3. Navigate to the folder containing 'install-synapse-byod.sh':
echo        cd /path/to/folder
echo   4. Run these exact two commands:
echo.
echo        chmod +x install-synapse-byod.sh
echo        sudo ./install-synapse-byod.sh
echo.
echo The script will automatically install, import classroom keys,
echo configure firewalls, and connect to Synapse Command.
echo ====================================================================
pause
exit /b 0
