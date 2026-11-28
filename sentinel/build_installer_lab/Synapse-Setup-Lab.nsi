Unicode True
RequestExecutionLevel admin
CRCCheck off
SetCompressor /SOLID lzma

Name "Synapse Lab Setup (Domain Computers)"
OutFile "dist\Synapse-Setup-Lab.exe"
Icon "synapse-command.ico"
InstallDir "$PROGRAMFILES64\DigiAgent"
ShowInstDetails show

Page directory
Page instfiles

Section "MainSection" SEC01
    SetShellVarContext all

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

    DetailPrint "Configuring Service Auto-Recovery..."
    ExecWait 'sc config DigiAgentService start= auto'
    ExecWait 'sc failure DigiAgentService reset= 0 actions= restart/1000/restart/1000/restart/1000'
    ExecWait 'sc start DigiAgentService'

    DetailPrint "Configuring Domain Logon Policy (Enabling Other User for Domain Login)..."
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" "DontDisplayLastUserName" 1
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" "HideFastUserSwitching" 0
    WriteRegDWORD HKLM "SOFTWARE\Policies\Microsoft\Windows\System" "HideFastUserSwitching" 0

    IfSilent skip_msgbox
    MessageBox MB_OK|MB_ICONINFORMATION "Synapse Lab Client Installed Successfully!$\r$\n$\r$\n• Configured for Domain Authentication ('Other user' active)$\r$\n• Classroom Authentication Keys Deployed$\r$\n• Core Monitoring Service Active$\r$\n• Zero BYOD Restrictions"
skip_msgbox:

SectionEnd
