@echo off
:: Synapse Turn OFF (Student Privacy Mode)
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo Stopping Synapse Agent...
net stop DigiAgentService >nul 2>&1
net stop VeyonService >nul 2>&1
taskkill /f /im digiagent-server.exe >nul 2>&1
taskkill /f /im veyon-server.exe >nul 2>&1

echo ==========================================================
echo  [PRIVACY ON] Synapse Classroom Monitoring is turned OFF!
echo  No screen sharing, remote control, or network monitoring.
echo ==========================================================
pause
