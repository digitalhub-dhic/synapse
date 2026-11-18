@echo off
:: ====================================================================
::   SYNAPSE COMMAND - LINUX LAB WORKSTATION INSTALLER (.BAT)
::   Deploys Hardened Lab Client with Anti-Tamper & Auto-Updater:
::     chmod +x install-synapse-lab.sh
::     sudo ./install-synapse-lab.sh
:: ====================================================================
title Synapse Command - Linux Lab Workstation Installer
color 0A
setlocal enabledelayedexpansion

echo ====================================================================
echo      SYNAPSE COMMAND - LINUX LAB WORKSTATION DEPLOYMENT
echo ====================================================================
echo.
echo  Target: Permanent Lab Computers (DIGIHUB Lab Workstations)
echo  Script: install-synapse-lab.sh
echo  Includes:
echo    - Hardened Systemd Watchdog (synapse-agent.service)
echo    - Anti-Tamper / Auto-Restart on Process Kill
echo    - Background Hourly Auto-Updater Cron Job
echo.
echo  Choose deployment method:
echo    [1] Remote Lab PC over Network (Wi-Fi / LAN via SSH)
echo    [2] Direct Linux Terminal Instructions
echo    [3] Exit
echo.
set /p mode="Select option [1-3] (Default is 1): "
if "%mode%"=="" set mode=1

if "%mode%"=="1" goto REMOTE_LAB
if "%mode%"=="2" goto SHOW_INSTRUCTIONS
if "%mode%"=="3" exit /b 0
goto REMOTE_LAB

:REMOTE_LAB
echo.
echo --------------------------------------------------------------------
echo  REMOTE LAB WORKSTATION DEPLOYMENT
echo --------------------------------------------------------------------
set /p target_ip=" Enter Lab PC IP Address (e.g. 192.168.1.101): "
if "%target_ip%"=="" (
    echo [ERROR] IP address cannot be empty.
    pause
    exit /b 1
)

set /p target_user=" Enter Linux Admin Username [default: administrator]: "
if "%target_user%"=="" set target_user=administrator

echo.
echo [*] Testing network reachability to %target_ip%...
ping -n 1 -w 1500 %target_ip% >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] Device %target_ip% did not respond to ping. Attempting connection anyway...
) else (
    echo [OK] Lab PC is reachable on the network.
)

echo.
echo [*] Copying install-synapse-lab.sh to %target_user%@%target_ip%...
scp -o StrictHostKeyChecking=no "%~dp0install-synapse-lab.sh" %target_user%@%target_ip%:~/install-synapse-lab.sh
if %errorlevel% neq 0 (
    echo.
    echo [!] Failed to copy installer to remote Lab PC.
    echo     Please verify:
    echo       1. Target IP (%target_ip%) and Username (%target_user%) are correct.
    echo       2. SSH service is running on the Lab PC ('sudo systemctl start ssh').
    echo.
    pause
    exit /b 1
)

echo.
echo [*] Running: chmod +x ~/install-synapse-lab.sh ^&^& sudo ~/install-synapse-lab.sh
echo [*] (Enter sudo password when prompted)
echo --------------------------------------------------------------------
ssh -t -o StrictHostKeyChecking=no %target_user%@%target_ip% "chmod +x ~/install-synapse-lab.sh && sudo ~/install-synapse-lab.sh"

echo.
echo ====================================================================
echo  Lab workstation installation finished!
echo  The PC is now connected and protected by Anti-Tamper.
echo ====================================================================
pause
exit /b 0

:SHOW_INSTRUCTIONS
cls
echo ====================================================================
echo     DIRECT EXECUTION INSTRUCTIONS ON LINUX LAB WORKSTATION
echo ====================================================================
echo.
echo On the Lab Workstation:
echo   1. Open Terminal.
echo   2. Navigate to the folder containing 'install-synapse-lab.sh'.
echo   3. Run:
echo.
echo        chmod +x install-synapse-lab.sh
echo        sudo ./install-synapse-lab.sh
echo.
echo ====================================================================
pause
exit /b 0
