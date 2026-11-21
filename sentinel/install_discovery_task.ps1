$action = New-ScheduledTaskAction -Execute 'pythonw.exe' -Argument 'C:\SynapseAISentinel\SynapseFleetDiscovery.py' -WorkingDirectory 'C:\SynapseAISentinel'
$triggerStartup = New-ScheduledTaskTrigger -AtStartup
$triggerLogon = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Days 365) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName 'SynapseFleetDiscovery' -Action $action -Trigger @($triggerStartup, $triggerLogon) -Settings $settings -User 'SYSTEM' -RunLevel Highest -Force
Start-ScheduledTask -TaskName 'SynapseFleetDiscovery'
Write-Host "[OK] SynapseFleetDiscovery background task installed and started successfully!"
