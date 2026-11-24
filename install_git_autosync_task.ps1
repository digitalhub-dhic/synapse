$action = New-ScheduledTaskAction -Execute "pythonw.exe" -Argument '"C:\Users\administrator\projects\synapse\watch_app_updates.pyw"'
$triggerLogon = New-ScheduledTaskTrigger -AtLogOn
$triggerRep = New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 10) -RepetitionDuration ([TimeSpan]::MaxValue)
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable

Register-ScheduledTask -TaskName "SynapseGitAutoSync" -Action $action -Trigger @($triggerLogon, $triggerRep) -Principal $principal -Settings $settings -Force
Start-ScheduledTask -TaskName "SynapseGitAutoSync"
Write-Host "SynapseGitAutoSync registered and started successfully!"
