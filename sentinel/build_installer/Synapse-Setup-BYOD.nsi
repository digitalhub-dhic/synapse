Unicode True
RequestExecutionLevel admin
CRCCheck off
SetCompressor /SOLID lzma

Name "Synapse Hardened BYOD Setup"
OutFile "dist\Synapse-Setup-BYOD-NSIS.exe"
InstallDir "$PROGRAMFILES64\DigiAgent"
ShowInstDetails show

; BYOD enrollment must be a visible, affirmative choice before any controls
; or background services are installed. The batch bootstrap checks the fresh
; token written after a successful installation, so an older/silent package
; cannot silently continue onboarding.
LicenseData "BYOD-Monitoring-Consent.txt"
Page license
Page directory
Page instfiles

Section "MainSection" SEC01
    SetShellVarContext all

    DetailPrint "Terminating previous guard processes..."
    ExecWait 'taskkill /F /IM SynapseWatchdog.exe'
    ExecWait 'taskkill /F /IM SynapseBYODGuard.exe'

    DetailPrint "Extracting and installing core Synapse client..."
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File "Synapse-Setup.exe"
    ExecWait '"$PLUGINSDIR\Synapse-Setup.exe" /S' $0
    IntCmp $0 0 core_install_ok core_install_failed core_install_failed
core_install_failed:
    MessageBox MB_ICONSTOP "The DigiAgent client installation failed (exit code $0). No BYOD enrollment was completed."
    Abort
core_install_ok:

    ; Do not replace runtime DLLs while the service still has them loaded.
    ExecWait 'powershell.exe -NoProfile -Command "Stop-Service -Name DigiAgentService -Force -ErrorAction SilentlyContinue; (Get-Service -Name DigiAgentService).WaitForStatus(\"Stopped\", [TimeSpan]::FromSeconds(15))"' $0
    IntCmp $0 0 service_stopped service_stop_failed service_stop_failed
service_stop_failed:
    MessageBox MB_ICONSTOP "DigiAgentService did not stop cleanly (exit code $0). Runtime files were not replaced."
    Abort
service_stopped:

    DetailPrint "Deploying Classroom Authentication Keys..."
    CreateDirectory "$APPDATA\DigiAgent\keys\public\Digihub"
    SetOutPath "$APPDATA\DigiAgent\keys\public\Digihub"
    File /oname=key "Digihub.key"

    CreateDirectory "$APPDATA\Veyon\keys\public\Digihub"
    SetOutPath "$APPDATA\Veyon\keys\public\Digihub"
    File /oname=key "Digihub.key"

    DetailPrint "Configuring College Network Connection..."
    # Pre-configure Tailscale Enterprise Registry Policies (Key provisioned securely via MDM/Policy)
    WriteRegStr HKLM "SOFTWARE\Policies\Tailscale" "UnattendedMode" "always"

    # Install MSI
    SetOutPath "$PLUGINSDIR"
    File "tailscale.msi"
    ExecWait 'msiexec /i "$PLUGINSDIR\tailscale.msi" /quiet /norestart' $0
    IntCmp $0 0 tailscale_install_ok tailscale_install_failed tailscale_nonzero
tailscale_nonzero:
    IntCmp $0 3010 tailscale_install_ok tailscale_install_failed tailscale_install_failed
tailscale_install_failed:
    MessageBox MB_ICONSTOP "Tailscale installation failed (exit code $0). No BYOD enrollment was completed."
    Abort
tailscale_install_ok:

    # Ensure service is running
    ExecWait 'sc config Tailscale start= auto'
    ExecWait 'net start Tailscale'

    DetailPrint "Keeping Tailscale and Synapse visible to the student."

    DetailPrint "Cleaning legacy startup entries..."
    DeleteRegValue HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "SynapseBYODGuard"
    Delete "$SMSTARTUP\SynapseBYODGuard.lnk"
    Delete "$SMSTARTUP\SynapseBYODGuard.bat"
    SetShellVarContext current
    Delete "$SMSTARTUP\SynapseBYODGuard.lnk"
    Delete "$SMSTARTUP\SynapseBYODGuard.bat"
    SetShellVarContext all
    ExecWait 'schtasks /Delete /TN SynapseWatchdog /F'
    ExecWait 'schtasks /Delete /TN SynapseBYODGuard /F'

    DetailPrint "Deploying Wi-Fi Disconnection Guard..."
    SetOutPath "$INSTDIR"
    File "SynapseBYODGuard.exe"
    File "SynapseWatchdog.exe"
    File "DigiAgentCore.dll"

    SetOutPath "$INSTDIR\plugins"
    File "windows-platform.dll"
    File "remoteaccess.dll"
    File "screenlock.dll"

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "SynapseBYODGuard" '"$INSTDIR\SynapseBYODGuard.exe"'
    ExecWait 'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$$a = New-ScheduledTaskAction -Execute \"$INSTDIR\SynapseBYODGuard.exe\"; $$t = New-ScheduledTaskTrigger -AtLogOn; $$s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries; $$p = New-ScheduledTaskPrincipal -GroupId \"S-1-5-32-545\" -RunLevel Highest; Register-ScheduledTask -TaskName \"SynapseBYODGuard\" -Action $$a -Trigger $$t -Settings $$s -Principal $$p -Force"'
    Exec '"$INSTDIR\SynapseBYODGuard.exe"'

    ExecWait 'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$$a = New-ScheduledTaskAction -Execute \"$INSTDIR\SynapseWatchdog.exe\"; $$t1 = New-ScheduledTaskTrigger -AtStartup; $$t2 = New-ScheduledTaskTrigger -AtLogOn; Register-ScheduledTask -TaskName \"SynapseWatchdog\" -Action $$a -Trigger @($$t1,$$t2) -User \"SYSTEM\" -RunLevel Highest -Force"' $0
    IntCmp $0 0 watchdog_task_ok watchdog_task_failed watchdog_task_failed
watchdog_task_failed:
    MessageBox MB_ICONSTOP "Could not register the required Synapse watchdog (exit code $0). Enrollment is incomplete."
    Abort
watchdog_task_ok:

    DetailPrint "Configuring Service Auto-Recovery..."
    ExecWait 'sc config DigiAgentService start= auto'
    ExecWait 'sc failure DigiAgentService reset= 0 actions= restart/1000/restart/1000/restart/1000'
    ExecWait 'sc start DigiAgentService'

    DetailPrint "Configuring BYOD Logon Policy (Disabling Other User and Switch User)..."
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" "DontDisplayLastUserName" 0
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" "HideFastUserSwitching" 1
    WriteRegDWORD HKLM "SOFTWARE\Policies\Microsoft\Windows\System" "HideFastUserSwitching" 1

    ; Record affirmative consent and this install attempt for the bootstrap.
    ; Keep the record visible with the installed Synapse files.
    FileOpen $1 "$INSTDIR\byod-consent.ini" a
    FileWrite $1 "accepted_at=$%DATE% $%TIME% installer_windows_account=$%USERDOMAIN%\$%USERNAME%$\r$\n"
    FileClose $1
    FileOpen $1 "$INSTDIR\byod-consent-token.txt" w
    FileWrite $1 "$%SYNAPSE_CONSENT_NONCE%"
    FileClose $1

    MessageBox MB_OK|MB_ICONINFORMATION "Synapse components are installed. Return to the setup window and complete teacher password and updater setup before restarting."

finish:
SectionEnd
