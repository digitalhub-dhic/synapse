@echo off
title Connect All Classroom BYOD Devices
echo ========================================================
echo         CONNECTING ALL CLASSROOM BYOD DEVICES
echo ========================================================
echo Enforcing Synapse management across all student laptops...
python "C:\SynapseAISentinel\byod_fleet_manager.py" connect-all
echo.
echo [SUCCESS] Every device has been connected to classroom session!
timeout /t 3
exit
