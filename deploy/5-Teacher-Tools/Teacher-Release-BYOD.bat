@echo off
title Release All Classroom BYOD Devices
echo ========================================================
echo         RELEASING ALL CLASSROOM BYOD DEVICES
echo ========================================================
echo Releasing all student laptops so they can disconnect or switch Wi-Fi...
python "C:\SynapseAISentinel\byod_fleet_manager.py" release-all
echo.
echo [SUCCESS] Every BYOD laptop has been released!
echo Students can now safely disconnect, leave class, and take laptops home.
echo No kiosk or lock screen will appear outside the classroom.
timeout /t 5
