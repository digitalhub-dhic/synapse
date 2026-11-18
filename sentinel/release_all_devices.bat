@echo off
title Release All Classroom BYOD Devices
echo ========================================================
echo         RELEASING ALL CLASSROOM BYOD DEVICES
echo ========================================================
echo Releasing all student laptops so they can disconnect or switch Wi-Fi...
python "C:\SynapseAISentinel\byod_fleet_manager.py" release-all
echo.
echo [SUCCESS] Every device has been released!
timeout /t 3
exit
