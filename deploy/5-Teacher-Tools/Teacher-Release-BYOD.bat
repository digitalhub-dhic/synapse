@echo off
title Release All Classroom BYOD Devices
echo ========================================================
echo         RELEASING ALL CLASSROOM BYOD DEVICES
echo ========================================================
echo Releasing all student laptops so they can disconnect or switch Wi-Fi...
python "C:\SynapseAISentinel\byod_fleet_manager.py" release-all
echo.
echo Release requests were submitted.
echo Confirm each device status in the teacher dashboard before assuming release completed.
timeout /t 5
