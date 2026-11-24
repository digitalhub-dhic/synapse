$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '..\..\4-Auto-Updater\Check-And-AutoUpdate.ps1'
$validator = Join-Path $PSScriptRoot 'Validate-UpdaterPublisherPin.ps1'
$targetDir = 'C:\Program Files\DigiAgent'
$target = Join-Path $targetDir 'Check-And-AutoUpdate.ps1'

if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) {
    throw "Updater trust validator is missing: $validator"
}
& $validator $source
if (-not (Test-Path -LiteralPath $targetDir -PathType Container)) {
    throw "Synapse client installation directory is missing: $targetDir"
}

Copy-Item -LiteralPath $source -Destination $target -Force
& icacls.exe $target /inheritance:r /grant:r 'SYSTEM:(F)' 'Administrators:(F)' 'Users:(RX)' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not secure the local updater script ACL.' }

$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$target`""
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(2) -RepetitionInterval (New-TimeSpan -Minutes 30)
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName 'SynapseAutoUpdate' -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
Write-Host 'Registered SynapseAutoUpdate using the protected local updater script.'
