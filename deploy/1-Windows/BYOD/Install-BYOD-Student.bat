@echo off
:: Synapse Command BYOD Client Installer & Auto-Updater Registration v4.11.7.0
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [INFO] Requesting Administrator Privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo ==========================================================
echo     SYNAPSE COMMAND - BYOD STUDENT LAPTOP SETUP v4.11.7.0
echo ==========================================================

echo [*] Installing Synapse BYOD Client for Lab Access...
"%~dp0Synapse-Setup-BYOD.exe" /S

echo [*] Importing Classroom Security Key...
if exist "C:\Program Files\DigiAgent\digiagent-cli.exe" (
    "C:\Program Files\DigiAgent\digiagent-cli.exe" authkeys import Digihub/public "%~dp0..\..\3-Security-Keys\Public-Key (Safe for All Computers)\Digihub.pem" >nul 2>&1
)

echo [*] Registering Background Auto-Updater...
schtasks /create /tn "SynapseAutoUpdate" /tr "powershell -ExecutionPolicy Bypass -WindowStyle Hidden -Command "& { if (Test-Path '\\fileserver\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1') { & '\\fileserver\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1' } else { & '\\192.168.1.50\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1' } }"" /sc minute /mo 30 /ru "SYSTEM" /f >nul 2>&1

echo [*] Enforcing Anti-Tamper Service Configuration...
sc config DigiAgentService start= auto >nul 2>&1
sc failure DigiAgentService reset= 0 actions= restart/1000/restart/2000/restart/5000 >nul 2>&1
net start DigiAgentService >nul 2>&1

echo [OK] BYOD Client v4.11.7.0 connected and registered for automatic fleet updates.
pause
