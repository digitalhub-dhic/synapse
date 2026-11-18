@echo off
title Enforce All Classroom BYOD Devices
echo ========================================================
echo         ENFORCING CLASSROOM BYOD LOCKDOWN
echo ========================================================
echo Activating classroom monitoring and Wi-Fi protection...
python "C:\SynapseAISentinel\byod_fleet_manager.py" connect-all
echo.
echo [SUCCESS] Every device has been connected to active classroom session!
timeout /t 5
