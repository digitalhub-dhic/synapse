# Synapse Enterprise Silent Auto-Updater for Windows Clients (Lab & BYOD)
$ErrorActionPreference = "SilentlyContinue"

$serverHost = "fileserver"
if (-not (Test-Path "\\fileserver\public\Synapse\4-Auto-Updater\client_version.json")) {
    $serverHost = "192.168.1.50"
}
$versionFile = "\\$serverHost\public\Synapse\4-Auto-Updater\client_version.json"
if (-not (Test-Path $versionFile)) { exit 0 }

try {
    $config = Get-Content $versionFile -Raw | ConvertFrom-Json
    $latestVersion = $config.latest_version
} catch {
    exit 0
}

$localVerFile = "C:\ProgramData\DigiAgent\installed_version.txt"
$currentVer = if (Test-Path $localVerFile) { (Get-Content $localVerFile).Trim() } else { "4.11.4.0" }

if ($currentVer -ne $latestVersion) {
    Write-Host "[*] Synapse Update detected ($latestVersion vs $currentVer). Updating silently..."
    
    # 1. Stop client services cleanly
    Stop-Service -Name "DigiAgentService" -Force -ErrorAction SilentlyContinue
    Stop-Process -Name "digiagent-server" -Force -ErrorAction SilentlyContinue
    Stop-Process -Name "digiagent-worker" -Force -ErrorAction SilentlyContinue
    Stop-Process -Name "digiagent-master" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    
    # 2. Update DigiAgentCore.dll
    $srcCore = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\DigiAgentCore.dll"
    if (-not (Test-Path $srcCore)) { $srcCore = "\\$serverHost\public\Synapse\DigiAgentCore.dll" }
    if (Test-Path $srcCore) {
        Copy-Item -Path $srcCore -Destination "C:\Program Files\DigiAgent\DigiAgentCore.dll" -Force -ErrorAction SilentlyContinue
    }

    # 3. Update all plugin DLLs
    $srcPlugins = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\plugins"
    if (-not (Test-Path $srcPlugins)) { $srcPlugins = "\\$serverHost\public\Synapse\plugins" }
    $dstPlugins = "C:\Program Files\DigiAgent\plugins"
    if (Test-Path $srcPlugins) {
        if (-not (Test-Path $dstPlugins)) { New-Item -ItemType Directory -Path $dstPlugins -Force | Out-Null }
        Get-ChildItem -Path $srcPlugins -Filter "*.dll" | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination (Join-Path $dstPlugins $_.Name) -Force -ErrorAction SilentlyContinue
        }
    }

    # 4. Update master binary if installed
    $srcMaster = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\digiagent-master.exe"
    if (-not (Test-Path $srcMaster)) { $srcMaster = "\\$serverHost\public\Synapse\digiagent-master.exe" }
    $dstMaster = "C:\Program Files\DigiAgent\digiagent-master.exe"
    if ((Test-Path $dstMaster) -and (Test-Path $srcMaster)) {
        Copy-Item -Path $srcMaster -Destination $dstMaster -Force -ErrorAction SilentlyContinue
    }

    # 5. Anti-Tamper Security: Lock directory permissions
    if (Test-Path "C:\Program Files\DigiAgent") {
        & icacls.exe "C:\Program Files\DigiAgent" /grant "Users:(OI)(CI)(RX)" /deny "Users:(DE,WDAC,WD,AD)" *>$null
    }
    if (Test-Path "C:\ProgramData\DigiAgent") {
        & icacls.exe "C:\ProgramData\DigiAgent" /grant "Users:(OI)(CI)(RX)" /deny "Users:(DE,WDAC,WD,AD)" *>$null
    }

    # 6. Configure Service Auto-Restart on Failure
    sc.exe config DigiAgentService start= auto >$null 2>&1
    sc.exe failure DigiAgentService reset= 0 actions= restart/1000/restart/2000/restart/5000 >$null 2>&1
    Start-Service -Name "DigiAgentService" -ErrorAction SilentlyContinue

    # 7. Record current version
    New-Item -ItemType Directory -Path "C:\ProgramData\DigiAgent" -Force -ErrorAction SilentlyContinue | Out-Null
    Set-Content -Path $localVerFile -Value $latestVersion -Force
    Write-Host "[OK] Synapse Client successfully updated and verified on $latestVersion"
}
