@echo off
:: Synapse Command Lab Workstation Silent Installer & Anti-Tamper Setup v4.11.7.0
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] Must run as Administrator!
    pause
    exit /b 1
)

echo ==========================================================
echo     SYNAPSE COMMAND - LAB WORKSTATION SETUP v4.11.7.0
echo ==========================================================

echo [*] Installing Synapse Lab Client v4.11.7.0...
"%~dp0Synapse-Setup-Lab.exe" /S

echo [*] Applying Anti-Tamper Security & Permissions...
sc config DigiAgentService start= auto >nul 2>&1
sc failure DigiAgentService reset= 0 actions= restart/1000/restart/2000/restart/5000 >nul 2>&1
net start DigiAgentService >nul 2>&1

:: Lock folders so students cannot delete, rename, or tamper with files
if exist "C:\Program Files\DigiAgent" (
    icacls "C:\Program Files\DigiAgent" /grant "Users:(OI)(CI)(RX)" /deny "Users:(DE,WDAC,WD,AD)" >nul 2>&1
)
if exist "C:\ProgramData\DigiAgent" (
    icacls "C:\ProgramData\DigiAgent" /grant "Users:(OI)(CI)(RX)" /deny "Users:(DE,WDAC,WD,AD)" >nul 2>&1
)

:: Register Auto-Update Scheduled Task (runs every 30 mins)
schtasks /create /tn "SynapseAutoUpdate" /tr "powershell -ExecutionPolicy Bypass -WindowStyle Hidden -Command "& { if (Test-Path '\\fileserver\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1') { & '\\fileserver\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1' } else { & '\\192.168.1.50\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1' } }"" /sc minute /mo 30 /ru "SYSTEM" /f >nul 2>&1

echo [OK] Synapse Lab Client v4.11.7.0 installed, protected, and auto-updating successfully!
