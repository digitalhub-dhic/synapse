@echo off
:: Synapse Command BYOD Client Installer & Auto-Updater Registration v4.11.7.0
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [INFO] Requesting Administrator Privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo ==========================================================
echo     SYNAPSE COMMAND - BYOD STUDENT LAPTOP SETUP v4.11.7.0
echo ==========================================================

if not exist "%~dp0Synapse-Setup-BYOD.exe" (
    echo [ERROR] Synapse-Setup-BYOD.exe is missing from this deployment package.
    exit /b 1
)
if not exist "%~dp0Set-SynapseTeacherPassword.ps1" (
    echo [ERROR] Set-SynapseTeacherPassword.ps1 is missing from this deployment package.
    exit /b 1
)
if not exist "%~dp0Install-LocalUpdater.ps1" (
    echo [ERROR] Install-LocalUpdater.ps1 is missing from this deployment package.
    exit /b 1
)
if not exist "%~dp0Validate-UpdaterPublisherPin.ps1" (
    echo [ERROR] Validate-UpdaterPublisherPin.ps1 is missing from this deployment package.
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Validate-UpdaterPublisherPin.ps1" "%~dp0..\..\4-Auto-Updater\Check-And-AutoUpdate.ps1"
if errorlevel 1 (
    echo [ERROR] The updater publisher pin is not configured. Set it before installing or enrolling this laptop.
    exit /b 1
)

echo [*] Installing Synapse BYOD Client for Lab Access...
set "SYNAPSE_CONSENT_NONCE=%RANDOM%-%RANDOM%-%RANDOM%-%RANDOM%"
set "SYNAPSE_CONSENT_TOKEN=C:\Program Files\DigiAgent\byod-consent-token.txt"
if exist "%SYNAPSE_CONSENT_TOKEN%" del /f /q "%SYNAPSE_CONSENT_TOKEN%" >nul 2>&1
"%~dp0Synapse-Setup-BYOD.exe"
if errorlevel 1 (
    echo [ERROR] BYOD client installation failed.
    exit /b 1
)
if not exist "%SYNAPSE_CONSENT_TOKEN%" (
    echo [CANCELLED] No fresh monitoring consent was recorded. This device was not enrolled.
    exit /b 2
)
findstr.exe /X /L /C:"%SYNAPSE_CONSENT_NONCE%" "%SYNAPSE_CONSENT_TOKEN%" >nul
if errorlevel 1 (
    echo [ERROR] The installer did not record fresh consent. Do not issue this laptop.
    exit /b 3
)
del /f /q "%SYNAPSE_CONSENT_TOKEN%" >nul 2>&1

echo [*] Setting this device's teacher override password...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Set-SynapseTeacherPassword.ps1"
if errorlevel 1 (
    echo [ERROR] Teacher password setup failed. Do not issue this laptop until setup succeeds.
    exit /b 1
)

echo [*] Registering locally protected Background Auto-Updater...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-LocalUpdater.ps1"
if errorlevel 1 (
    set "UPDATER_READY=0"
    echo [WARN] Secure updater registration failed. BYOD monitoring may work, but deployment is incomplete; notify the lab administrator.
) else (
    set "UPDATER_READY=1"
)
echo [INFO] Updates are accepted only from the locally pinned publisher; task registration does not prove an update succeeded.

echo [*] Enforcing Anti-Tamper Service Configuration...
sc.exe config DigiAgentService start= auto >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Could not configure DigiAgentService startup.
    exit /b 1
)
sc.exe failure DigiAgentService reset= 0 actions= restart/1000/restart/2000/restart/5000 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Could not configure DigiAgentService recovery.
    exit /b 1
)
powershell.exe -NoProfile -Command "$s=Get-Service -Name 'DigiAgentService' -ErrorAction Stop; if ($s.Status -ne 'Running') { Start-Service -Name 'DigiAgentService' -ErrorAction Stop; $s=Get-Service -Name 'DigiAgentService'; $s.WaitForStatus('Running',[TimeSpan]::FromSeconds(30)) }"
if errorlevel 1 (
    echo [ERROR] DigiAgentService did not reach the running state.
    exit /b 1
)

if "%UPDATER_READY%"=="1" (
    echo [OK] BYOD client and updater task installed. Verify monitoring and updates before issuing this laptop.
) else (
    echo [INCOMPLETE] BYOD client installed, but updater registration failed. Do not issue this laptop until fixed.
    pause
    exit /b 4
)
choice /C YN /N /M "Restart now to complete setup? [Y/N] "
if errorlevel 2 goto setup_complete
shutdown.exe /r /t 5
goto :eof

:setup_complete
echo Restart later to finish applying the classroom policy.
pause
