Unicode True
RequestExecutionLevel admin
CRCCheck off
SetCompressor /SOLID lzma

Name "Synapse Hardened BYOD Setup"
OutFile "dist\Synapse-Setup-BYOD.exe"
InstallDir "$PROGRAMFILES64\DigiAgent"
ShowInstDetails show

Page directory
Page instfiles

Section "MainSection" SEC01
    SetShellVarContext all

    DetailPrint "Terminating previous guard processes..."
    ExecWait 'taskkill /F /IM SynapseWatchdog.exe'
    ExecWait 'taskkill /F /IM SynapseBYODGuard.exe'
    ExecWait 'taskkill /F /IM tailscale-ipn.exe'

    DetailPrint "Extracting and installing core Synapse client..."
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File "Synapse-Setup.exe"
    ExecWait '"$PLUGINSDIR\Synapse-Setup.exe" /S'

    DetailPrint "Deploying Classroom Authentication Keys..."
    CreateDirectory "$APPDATA\DigiAgent\keys\public\Digihub"
    SetOutPath "$APPDATA\DigiAgent\keys\public\Digihub"
    File /oname=key "Digihub.key"

    CreateDirectory "$APPDATA\Veyon\keys\public\Digihub"
    SetOutPath "$APPDATA\Veyon\keys\public\Digihub"
    File /oname=key "Digihub.key"

    DetailPrint "Configuring Secure Network Mesh..."
    # Pre-configure Tailscale Enterprise Registry Policies (Key provisioned securely via MDM/Policy)
    WriteRegStr HKLM "SOFTWARE\Policies\Tailscale" "UnattendedMode" "always"
    WriteRegDWORD HKLM "SOFTWARE\Policies\Tailscale" "HideNetworkSettings" 1

    # Install MSI
    SetOutPath "$PLUGINSDIR"
    File "tailscale.msi"
    ExecWait 'msiexec /i "$PLUGINSDIR\tailscale.msi" /quiet /norestart'

    # Ensure service is running
    ExecWait 'sc config Tailscale start= auto'
    ExecWait 'net start Tailscale'

    # 4. Suppress the GUI popup window completely
    ExecWait 'taskkill /F /IM tailscale-ipn.exe'
    DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "Tailscale"
    DeleteRegValue HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "Tailscale"

    DetailPrint "Applying Anti-Uninstall Hardening..."
    Delete "$INSTDIR\uninstall.exe"
    DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Synapse"
    DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\DigiAgent"
    DeleteRegKey HKLM "SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Synapse"
    DeleteRegKey HKLM "SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\DigiAgent"
    # Hide Tailscale from Settings & Control Panel
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{54939609-8ED7-550F-95F0-CDE77EC4499A}" "SystemComponent" 1
    WriteRegDWORD HKLM "SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{54939609-8ED7-550F-95F0-CDE77EC4499A}" "SystemComponent" 1
    DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Tailscale"
    DeleteRegKey HKLM "SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Tailscale"

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

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "SynapseBYODGuard" '"$INSTDIR\SynapseBYODGuard.exe"'
    ExecWait 'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$$a = New-ScheduledTaskAction -Execute \"$INSTDIR\SynapseBYODGuard.exe\"; $$t = New-ScheduledTaskTrigger -AtLogOn; $$s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries; $$p = New-ScheduledTaskPrincipal -GroupId \"S-1-5-32-545\" -RunLevel Highest; Register-ScheduledTask -TaskName \"SynapseBYODGuard\" -Action $$a -Trigger $$t -Settings $$s -Principal $$p -Force"'
    Exec '"$INSTDIR\SynapseBYODGuard.exe"'

    DetailPrint "Configuring Service Auto-Recovery..."
    ExecWait 'sc config DigiAgentService start= auto'
    ExecWait 'sc failure DigiAgentService reset= 0 actions= restart/1000/restart/1000/restart/1000'
    ExecWait 'sc start DigiAgentService'

    DetailPrint "Configuring BYOD Logon Policy (Disabling Other User and Switch User)..."
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" "DontDisplayLastUserName" 0
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" "HideFastUserSwitching" 1
    WriteRegDWORD HKLM "SOFTWARE\Policies\Microsoft\Windows\System" "HideFastUserSwitching" 1

    MessageBox MB_YESNO|MB_ICONINFORMATION "Synapse BYOD Client Installed Successfully!$\r$\n$\r$\n• Connected to DIGIHUB Classroom & Cloud Mesh$\r$\n• Monitored on School Wi-Fi, Home Wi-Fi & Mobile Hotspots$\r$\n• Anti-Uninstall Protection Active$\r$\n• Offline Disconnection Lockdown Active$\r$\n• 'Other User' & 'Switch User' Disabled$\r$\n$\r$\nNOTE: Windows requires a Restart (or Sign Out) for the lock screen changes to appear.$\r$\n$\r$\nWould you like to restart now?" IDYES do_reboot IDNO finish

do_reboot:
    Reboot

finish:
SectionEnd
