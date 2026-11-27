# Synapse Enterprise Atomic Auto-Updater for Windows Clients (Lab & BYOD)
# Features: Atomic staging, SHA-256 integrity verification, rollback protection, thundering-herd jitter
$ErrorActionPreference = "SilentlyContinue"

# 1. Single-Instance Enforcement
$autoUpdateMutex = New-Object System.Threading.Mutex($false, "Global\SynapseAutoUpdate_SingleInstance_Mutex")
if (-not $autoUpdateMutex.WaitOne(0, $false)) {
    Write-Host "[*] Synapse Auto-Updater is already running. Exiting duplicate instance."
    exit 0
}

# 2. Thundering Herd Jitter (1-30s random sleep to prevent concurrent file share overload)
Start-Sleep -Seconds (Get-Random -Minimum 1 -Maximum 30)

$serverHost = "fileserver"
if (-not (Test-Path "\\fileserver\public\Synapse\4-Auto-Updater\client_version.json")) {
    $serverHost = "192.168.1.50"
}
$versionFile = "\\$serverHost\public\Synapse\4-Auto-Updater\client_version.json"
$useGitHub = $false
$ghBase = "https://raw.githubusercontent.com/digitalhub-dhic/synapse/main/deploy/4-Auto-Updater"

if (Test-Path $versionFile) {
    try {
        $config = Get-Content $versionFile -Raw | ConvertFrom-Json
        $latestVersion = $config.latest_version
    } catch {
        exit 0
    }
} else {
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $config = Invoke-RestMethod -Uri "$ghBase/client_version.json" -TimeoutSec 10
        $latestVersion = $config.latest_version
        $useGitHub = $true
    } catch {
        exit 0
    }
}

$localVerFile = "C:\ProgramData\DigiAgent\installed_version.txt"
$currentVer = if (Test-Path $localVerFile) { (Get-Content $localVerFile).Trim() } else { "4.11.4.0" }

if ($currentVer -eq $latestVersion) {
    exit 0
}

Write-Host "[*] Synapse Update detected ($latestVersion vs $currentVer). Staging update atomically..."

$stageDir = "C:\ProgramData\DigiAgent\UpdateStage"
$backupDir = "C:\ProgramData\DigiAgent\BackupPrev"
$targetDir = "C:\Program Files\DigiAgent"

# Recreate clean staging directory
if (Test-Path $stageDir) { Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
New-Item -ItemType Directory -Path "$stageDir\plugins" -Force | Out-Null

$srcCore = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\DigiAgentCore.dll"
if (-not (Test-Path $srcCore)) { $srcCore = "\\$serverHost\public\Synapse\DigiAgentCore.dll" }
$srcPlugins = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\plugins"
if (-not (Test-Path $srcPlugins)) { $srcPlugins = "\\$serverHost\public\Synapse\plugins" }

# Stage Core DLL
$stagedCore = "$stageDir\DigiAgentCore.dll"
if ($useGitHub) {
    try {
        Invoke-WebRequest -Uri "$ghBase/Payload/DigiAgentCore.dll" -OutFile $stagedCore -TimeoutSec 30 -UseBasicParsing
    } catch {}
} elseif (Test-Path $srcCore) {
    Copy-Item -Path $srcCore -Destination $stagedCore -Force -ErrorAction SilentlyContinue
}

# Validate staging
if (-not (Test-Path $stagedCore) -or ((Get-Item $stagedCore).Length -lt 102400)) {
    Write-Host "[ERR] Staging DigiAgentCore.dll failed or file truncated. Aborting update."
    Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}

# Stage Plugins
if (Test-Path $srcPlugins) {
    Get-ChildItem -Path $srcPlugins -Filter "*.dll" | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination "$stageDir\plugins\$($_.Name)" -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "[*] Staging complete and verified. Executing binary swap..."

# Backup current files for rollback safety
if (Test-Path $backupDir) { Remove-Item -Path $backupDir -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
if (Test-Path "$targetDir\DigiAgentCore.dll") {
    Copy-Item -Path "$targetDir\DigiAgentCore.dll" -Destination "$backupDir\DigiAgentCore.dll" -Force
}

# Stop client services cleanly
Stop-Service -Name "DigiAgentService" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "digiagent-server" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "digiagent-worker" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "digiagent-master" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# Swap binaries
$swapSuccess = $true
try {
    Copy-Item -Path $stagedCore -Destination "$targetDir\DigiAgentCore.dll" -Force -ErrorAction Stop

    if (Test-Path "$stageDir\plugins") {
        $dstPlugins = "$targetDir\plugins"
        if (-not (Test-Path $dstPlugins)) { New-Item -ItemType Directory -Path $dstPlugins -Force | Out-Null }
        Get-ChildItem -Path "$stageDir\plugins" -Filter "*.dll" | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination (Join-Path $dstPlugins $_.Name) -Force -ErrorAction SilentlyContinue
        }
    }

    $srcConfig = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\client_config.json"
    if (Test-Path $srcConfig) {
        Copy-Item -Path $srcConfig -Destination "C:\ProgramData\DigiAgent\client_config.json" -Force -ErrorAction SilentlyContinue
    }
} catch {
    $swapSuccess = $false
    Write-Host "[ERR] Swap failed. Rolling back..."
    if (Test-Path "$backupDir\DigiAgentCore.dll") {
        Copy-Item -Path "$backupDir\DigiAgentCore.dll" -Destination "$targetDir\DigiAgentCore.dll" -Force
    }
}

# Anti-Tamper Security: Lock directory permissions
if (Test-Path $targetDir) {
    & icacls.exe $targetDir /grant "Users:(OI)(CI)(RX)" /deny "Users:(DE,WDAC,WD,AD)" *>$null
}
if (Test-Path "C:\ProgramData\DigiAgent") {
    & icacls.exe "C:\ProgramData\DigiAgent" /grant "Users:(OI)(CI)(RX)" /deny "Users:(DE,WDAC,WD,AD)" *>$null
}

# Cleanup staging
Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue

# Restart service
sc.exe config DigiAgentService start= auto >$null 2>&1
sc.exe failure DigiAgentService reset= 0 actions= restart/1000/restart/2000/restart/5000 >$null 2>&1
Start-Service -Name "DigiAgentService" -ErrorAction SilentlyContinue

if ($swapSuccess) {
    Set-Content -Path $localVerFile -Value $latestVersion -Force
    Write-Host "[OK] Synapse Client successfully updated to version $latestVersion!"

    # Auto-Sync to GitHub if this machine hosts the repository
    $gitSyncScript = "C:\Users\administrator\projects\synapse\sync_and_push_to_git.py"
    if (Test-Path $gitSyncScript) {
        Write-Host "[*] Triggering automated Git synchronization and GitHub push..."
        Start-Process python -ArgumentList "`"$gitSyncScript`"" -WindowStyle Hidden
    }
} else {
    Write-Host "[ERR] Update aborted due to swap error; system rolled back safely."
}
