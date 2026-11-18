@echo off
:: Synapse Turn ON (Classroom Mode)
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo Starting Synapse Agent...
net start DigiAgentService >nul 2>&1
net start VeyonService >nul 2>&1

echo ==========================================================
echo  [ONLINE] Synapse Classroom Monitoring is turned ON!
echo  Connected to teacher console for screen sharing and demo.
echo ==========================================================
pause
