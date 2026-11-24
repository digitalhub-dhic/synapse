@echo off
:: Synapse Command Lab Workstation Silent Installer & Anti-Tamper Setup v4.11.7.0
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] Must run as Administrator!
    pause
    exit /b 1
)

echo ==========================================================
echo     SYNAPSE COMMAND - LAB WORKSTATION SETUP v4.11.7.0
echo ==========================================================

echo [*] Installing Synapse Lab Client v4.11.7.0...
if not exist "%~dp0Synapse-Setup-Lab.exe" (
    echo [ERROR] Synapse-Setup-Lab.exe is missing.
    exit /b 1
)
"%~dp0Synapse-Setup-Lab.exe" /S
if errorlevel 1 (
    echo [ERROR] Synapse lab client installation failed.
    exit /b 1
)

echo [*] Applying Anti-Tamper Security & Permissions...
sc.exe config DigiAgentService start= auto >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Could not configure DigiAgentService.
    exit /b 1
)
sc.exe failure DigiAgentService reset= 0 actions= restart/1000/restart/2000/restart/5000 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Could not configure service recovery for DigiAgentService.
    exit /b 1
)
powershell.exe -NoProfile -Command "$s=Get-Service -Name 'DigiAgentService' -ErrorAction Stop; if ($s.Status -ne 'Running') { Start-Service -Name 'DigiAgentService' -ErrorAction Stop; $s=Get-Service -Name 'DigiAgentService'; $s.WaitForStatus('Running',[TimeSpan]::FromSeconds(30)) }"
if errorlevel 1 (
    echo [ERROR] DigiAgentService did not reach the running state.
    exit /b 1
)

:: Preserve administrator repair access; deny rules for Users also match
:: administrators' unelevated tokens and can break future maintenance.
if exist "C:\Program Files\DigiAgent" (
    icacls "C:\Program Files\DigiAgent" /inheritance:r /T /C /remove:d *S-1-5-32-545 /grant:r "*S-1-5-18:(OI)(CI)(F)" "*S-1-5-32-544:(OI)(CI)(F)" "*S-1-5-32-545:(OI)(CI)(RX)" >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Could not secure permissions on the client installation.
        exit /b 1
    )
)
if exist "C:\ProgramData\DigiAgent" (
    icacls "C:\ProgramData\DigiAgent" /inheritance:r /T /C /remove:d *S-1-5-32-545 /grant:r "*S-1-5-18:(OI)(CI)(F)" "*S-1-5-32-544:(OI)(CI)(F)" "*S-1-5-32-545:(OI)(CI)(RX)" >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Could not secure permissions on the client data directory.
        exit /b 1
    )
)

:: Register Auto-Update Scheduled Task (runs every 30 mins)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\BYOD\Install-LocalUpdater.ps1"
if errorlevel 1 echo [WARN] Secure updater registration failed. Notify the lab administrator.

echo [OK] Synapse Lab Client v4.11.7.0 installed. Updater trust depends on the pinned publisher signature.
