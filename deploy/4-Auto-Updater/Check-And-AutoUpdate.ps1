# Synapse Authenticated Auto-Updater for Windows Clients (Lab & BYOD)
# Updates are accepted only when every staged executable has a valid signature
# from the publisher thumbprint pinned locally in this protected script.
$ErrorActionPreference = "Stop"
# Replace this placeholder with the SHA-1 thumbprint of the publisher
# certificate before packaging the updater. It must remain local and protected;
# never load the trust anchor from the network share being checked.
$trustedSigner = 'REPLACE_WITH_PUBLISHER_CERTIFICATE_THUMBPRINT'

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
if (-not (Test-Path $versionFile)) { exit 0 }

try {
    $config = Get-Content $versionFile -Raw | ConvertFrom-Json
    $latestVersion = $config.latest_version
} catch {
    exit 0
}

if ($latestVersion -notmatch '^\d+(\.\d+){1,3}$' -or $trustedSigner -notmatch '^[0-9A-Fa-f]{40}$') {
    Write-Host '[ERR] Updater has no valid locally pinned Authenticode publisher thumbprint.'
    exit 1
}
$trustedSigner = $trustedSigner.ToUpperInvariant()

function Test-PinnedSignature([string]$Path, [string]$Thumbprint) {
    try {
        $signature = Get-AuthenticodeSignature -LiteralPath $Path
        return ($signature.Status -eq 'Valid' -and
            $null -ne $signature.SignerCertificate -and
            $signature.SignerCertificate.Thumbprint.ToUpperInvariant() -eq $Thumbprint)
    } catch { return $false }
}

$localVerFile = "C:\ProgramData\DigiAgent\installed_version.txt"
$currentVer = if (Test-Path $localVerFile) { (Get-Content $localVerFile -Raw).Trim() } else { "4.11.4.0" }
try {
    $latestVersionObject = [version]$latestVersion
    $currentVersionObject = [version]$currentVer
} catch {
    Write-Host '[ERR] Installed or published version is invalid; refusing to update.'
    exit 1
}

if ($currentVersionObject.CompareTo($latestVersionObject) -gt 0) {
    Write-Host "[ERR] Published version $latestVersion is older than installed version $currentVer; refusing downgrade."
    exit 1
}
if ($currentVersionObject.CompareTo($latestVersionObject) -eq 0) {
    exit 0
}

Write-Host "[*] Synapse Update detected ($latestVersion vs $currentVer). Staging and verifying update..."

$stageDir = "C:\ProgramData\DigiAgent\UpdateStage"
$backupDir = "C:\ProgramData\DigiAgent\BackupPrev"
$targetDir = "C:\Program Files\DigiAgent"

# Recreate clean staging directory
if (Test-Path $stageDir) { Remove-Item -Path $stageDir -Recurse -Force -ErrorAction Stop }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
New-Item -ItemType Directory -Path "$stageDir\plugins" -Force | Out-Null

$srcCore = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\DigiAgentCore.dll"
if (-not (Test-Path $srcCore)) { $srcCore = "\\$serverHost\public\Synapse\DigiAgentCore.dll" }
$srcPlugins = "\\$serverHost\public\Synapse\4-Auto-Updater\Payload\plugins"
if (-not (Test-Path $srcPlugins)) { $srcPlugins = "\\$serverHost\public\Synapse\plugins" }
if (-not (Test-Path $srcPlugins -PathType Container)) {
    Write-Host '[ERR] Plugin payload directory is missing. Update rejected.'
    Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}
$sourcePluginFiles = @(Get-ChildItem -Path $srcPlugins -Filter '*.dll' -File -ErrorAction SilentlyContinue)
if ($sourcePluginFiles.Count -eq 0) {
    Write-Host '[ERR] Plugin payload contains no DLLs. Update rejected.'
    Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}

# Stage Core DLL
$stagedCore = "$stageDir\DigiAgentCore.dll"
if (Test-Path $srcCore) {
    Copy-Item -Path $srcCore -Destination $stagedCore -Force -ErrorAction SilentlyContinue
}

# Validate staging
if (-not (Test-Path $stagedCore) -or ((Get-Item $stagedCore).Length -lt 102400)) {
    Write-Host "[ERR] Staging DigiAgentCore.dll failed or file truncated. Aborting update."
    Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}
if (-not (Test-PinnedSignature $stagedCore $trustedSigner)) {
    Write-Host '[ERR] Core DLL signature is missing, invalid, or from an untrusted publisher. Update rejected.'
    Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}

# Stage Plugins
if (Test-Path $srcPlugins) {
    foreach ($pluginSource in $sourcePluginFiles) {
        Copy-Item -LiteralPath $pluginSource.FullName -Destination "$stageDir\plugins\$($pluginSource.Name)" -Force -ErrorAction Stop
    }
}

$stagedPluginFiles = @(Get-ChildItem -Path "$stageDir\plugins" -Filter '*.dll' -File -ErrorAction SilentlyContinue)
foreach ($plugin in $stagedPluginFiles) {
    if (-not (Test-PinnedSignature $plugin.FullName $trustedSigner)) {
        Write-Host "[ERR] Plugin signature rejected: $($plugin.Name)"
        Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue
        exit 1
    }
}

Write-Host "[*] Staging complete and verified. Executing binary swap..."

# Backup current files for rollback safety
if (Test-Path $backupDir) { Remove-Item -Path $backupDir -Recurse -Force -ErrorAction Stop }
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
if (Test-Path "$targetDir\DigiAgentCore.dll") {
    Copy-Item -Path "$targetDir\DigiAgentCore.dll" -Destination "$backupDir\DigiAgentCore.dll" -Force -ErrorAction Stop
} else {
    Write-Host '[ERR] Existing core DLL is missing; refusing an update without a rollback copy.'
    exit 1
}
$hadPluginDir = Test-Path "$targetDir\plugins" -PathType Container
if ($hadPluginDir) {
    Copy-Item -Path "$targetDir\plugins" -Destination "$backupDir\plugins" -Recurse -Force -ErrorAction Stop
}

# Swap files and verify permissions/service before committing the version.
try {
    $service = Get-Service -Name 'DigiAgentService'
    if ($service.Status -ne 'Stopped') {
        Stop-Service -InputObject $service -Force -ErrorAction Stop
        $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    }
    foreach ($processName in @('digiagent-server', 'digiagent-worker', 'digiagent-master')) {
        Get-Process -Name $processName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction Stop
    }

    Copy-Item -Path $stagedCore -Destination "$targetDir\DigiAgentCore.dll" -Force -ErrorAction Stop

    $dstPlugins = "$targetDir\plugins"
    if (Test-Path $dstPlugins) { Remove-Item -Path $dstPlugins -Recurse -Force -ErrorAction Stop }
    New-Item -ItemType Directory -Path $dstPlugins -Force | Out-Null
    foreach ($plugin in $stagedPluginFiles) {
        Copy-Item -LiteralPath $plugin.FullName -Destination (Join-Path $dstPlugins $plugin.Name) -Force -ErrorAction Stop
    }

    $aclArgs = @('/inheritance:r', '/T', '/C', '/remove:d', '*S-1-5-32-545', '/grant:r',
        '*S-1-5-18:(OI)(CI)(F)', '*S-1-5-32-544:(OI)(CI)(F)', '*S-1-5-32-545:(OI)(CI)(RX)')
    foreach ($managedDir in @($targetDir, 'C:\ProgramData\DigiAgent')) {
        & icacls.exe $managedDir @aclArgs *> $null
        if ($LASTEXITCODE -ne 0) { throw "Could not apply managed permissions to $managedDir." }
    }

    $guardLog = 'C:\ProgramData\DigiAgent\guard.log'
    if (Test-Path $guardLog) {
        & icacls.exe $guardLog /grant:r '*S-1-5-32-545:(W)' *> $null
        if ($LASTEXITCODE -ne 0) { throw 'Could not restore the BYOD guard log write permission.' }
    }

    Set-Service -Name 'DigiAgentService' -StartupType Automatic -ErrorAction Stop
    Start-Service -Name 'DigiAgentService' -ErrorAction Stop
    (Get-Service -Name 'DigiAgentService').WaitForStatus('Running', [TimeSpan]::FromSeconds(45))

    $versionTemp = "$localVerFile.tmp"
    Set-Content -LiteralPath $versionTemp -Value $latestVersion -Encoding ascii -ErrorAction Stop
    Move-Item -LiteralPath $versionTemp -Destination $localVerFile -Force -ErrorAction Stop
} catch {
    $updateFailure = $_
    Write-Host "[ERR] Update failed; restoring the prior files. $updateFailure"
    $rollbackFailure = $null
    try {
        Stop-Service -Name 'DigiAgentService' -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath "$backupDir\DigiAgentCore.dll" -Destination "$targetDir\DigiAgentCore.dll" -Force -ErrorAction Stop
        if (Test-Path "$targetDir\plugins") {
            Remove-Item -LiteralPath "$targetDir\plugins" -Recurse -Force -ErrorAction Stop
        }
        if ($hadPluginDir) {
            Copy-Item -LiteralPath "$backupDir\plugins" -Destination "$targetDir\plugins" -Recurse -Force -ErrorAction Stop
        }
        Set-Service -Name 'DigiAgentService' -StartupType Automatic -ErrorAction Stop
        Start-Service -Name 'DigiAgentService' -ErrorAction Stop
        (Get-Service -Name 'DigiAgentService').WaitForStatus('Running', [TimeSpan]::FromSeconds(45))
    } catch {
        $rollbackFailure = $_
    }
    if ($rollbackFailure) {
        throw "CRITICAL: Update failed and rollback could not be verified. $rollbackFailure"
    }
    throw "Update failed; the previous files were restored and the service is running. $updateFailure"
} finally {
    if (Test-Path $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host "[OK] Synapse Client updated to version $latestVersion and its service is running."
