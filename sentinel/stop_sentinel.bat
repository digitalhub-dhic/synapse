@echo off
title Stop Synapse AI Sentinel
echo Stopping Synapse AI Sentinel Daemon...
powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*sentinel_daemon.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
echo Sentinel stopped.
timeout /t 2 > nul
exit
