@echo off
title Enforce All Classroom BYOD Devices
echo ========================================================
echo         ENFORCING CLASSROOM BYOD LOCKDOWN
echo ========================================================
echo Activating classroom monitoring and Wi-Fi protection...
python "C:\SynapseAISentinel\byod_fleet_manager.py" connect-all
echo.
echo Connect requests were submitted.
echo Confirm each device status in the teacher dashboard before the session begins.
timeout /t 5
