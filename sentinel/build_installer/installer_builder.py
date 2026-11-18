"""
Synapse Hardened BYOD Client Installer
Enterprise Hardened Client:
1. Elevates to Administrator automatically (UAC).
2. Installs core Synapse Client silently.
3. Deploys public authentication key.
4. PERMANENT ANTI-UNINSTALL LOCKDOWN:
   - Deletes uninstall.exe from disk.
   - Deletes all Uninstall keys from Windows Registry.
   - Synapse DOES NOT APPEAR in Control Panel or Windows Settings.
5. ANTI-TAMPER WI-FI ENFORCEMENT GUARD:
   - Runs in the user session with HIGHEST elevation.
   - Does NOT appear in Settings -> Startup Apps (no user toggles).
   - If Wi-Fi is disconnected or switched to unauthorized network, immediately locks screen.
   - Displays full-screen lockdown overlay with [Connect to Wi-Fi] and [Teacher Unlock].
   - Auto-unlocks the instant Wi-Fi reconnects.
6. Service Hardening & Auto-Recovery.
"""

import os
import sys
import time
import shutil
import ctypes
import subprocess
import tkinter as tk
from tkinter import messagebox

INSTALL_DIR = r"C:\Program Files\DigiAgent"
KEY_DIR = r"C:\ProgramData\DigiAgent\keys\public\Digihub"

def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False

def get_bundle_dir():
    if hasattr(sys, '_MEIPASS'):
        return sys._MEIPASS
    return os.path.dirname(os.path.abspath(__file__))

def run_installation():
    bundle_dir = get_bundle_dir()
    setup_src = os.path.join(bundle_dir, "Synapse-Setup.exe")
    key_src = os.path.join(bundle_dir, "Digihub.key")
    guard_src = os.path.join(bundle_dir, "SynapseBYODGuard.exe")

    # Terminate existing guard processes if updating
    subprocess.run(["taskkill", "/F", "/IM", "SynapseWatchdog.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "SynapseBYODGuard.exe"], capture_output=True)

    temp_dir = r"C:\Windows\Temp"
    os.makedirs(temp_dir, exist_ok=True)
    temp_setup = os.path.join(temp_dir, "Synapse-Core-Setup.exe")

    # 1. Silently install core Synapse client
    try:
        shutil.copyfile(setup_src, temp_setup)
        subprocess.run([temp_setup, "/S"], capture_output=True, timeout=120)
    except Exception as e:
        messagebox.showerror("Setup Error", f"Core installation failed: {e}")
        return False

    # Wait for files to unpack
    for _ in range(25):
        if os.path.exists(os.path.join(INSTALL_DIR, "digiagent-service.exe")):
            break
        time.sleep(1)

    # 1b. Deploy latest high-power input blocking engine and DLLs
    for dll in ["DigiAgentCore.dll"]:
        s = os.path.join(bundle_dir, dll)
        if os.path.exists(s):
            try:
                shutil.copyfile(s, os.path.join(INSTALL_DIR, dll))
            except Exception:
                pass
    plugins_dst = os.path.join(INSTALL_DIR, "plugins")
    os.makedirs(plugins_dst, exist_ok=True)
    for p_dll in ["windows-platform.dll", "remoteaccess.dll", "screenlock.dll"]:
        s = os.path.join(bundle_dir, p_dll)
        if os.path.exists(s):
            try:
                shutil.copyfile(s, os.path.join(plugins_dst, p_dll))
            except Exception:
                pass

    # 2. Deploy Authentication Keys
    try:
        os.makedirs(KEY_DIR, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(KEY_DIR, "key"))
        veyon_key_dir = r"C:\ProgramData\Veyon\keys\public\Digihub"
        os.makedirs(veyon_key_dir, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(veyon_key_dir, "key"))
    except Exception:
        pass

    # 3. PERMANENT ANTI-UNINSTALL LOCKDOWN
    uninst_file = os.path.join(INSTALL_DIR, "uninstall.exe")
    if os.path.exists(uninst_file):
        try:
            hidden_teacher_dir = r"C:\Windows\System32\synapse_master_guard"
            os.makedirs(hidden_teacher_dir, exist_ok=True)
            shutil.copyfile(uninst_file, os.path.join(hidden_teacher_dir, "uninst.bak"))
            os.remove(uninst_file)
        except Exception:
            pass

    # Wipe Uninstall registry entries so it never shows up in Control Panel
    for base in [r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
                 r"HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"]:
        for name in ["Synapse", "DigiAgent"]:
            subprocess.run(["reg", "delete", f"{base}\\{name}", "/f"], capture_output=True)

    # 4. CLEAN OLD STARTUP TOGGLES
    subprocess.run(["reg", "delete", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run", "/v", "SynapseBYODGuard", "/f"], capture_output=True)
    subprocess.run(["reg", "delete", r"HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run", "/v", "SynapseBYODGuard", "/f"], capture_output=True)
    for s_dir in [r"C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Startup",
                  os.path.expandvars(r"%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup")]:
        for fn in ["SynapseBYODGuard.lnk", "SynapseBYODGuard.bat"]:
            fp = os.path.join(s_dir, fn)
            if os.path.exists(fp):
                try:
                    os.remove(fp)
                except Exception:
                    pass

    # Clean old tasks
    subprocess.run(["schtasks", "/Delete", "/TN", "SynapseWatchdog", "/F"], capture_output=True)
    subprocess.run(["schtasks", "/Delete", "/TN", "SynapseBYODGuard", "/F"], capture_output=True)

    # 5. DEPLOY AUTO-HEALING WATCHDOG & GUARD
    try:
        watchdog_src = os.path.join(bundle_dir, "SynapseWatchdog.exe")
        if os.path.exists(watchdog_src):
            watchdog_dst = os.path.join(INSTALL_DIR, "SynapseWatchdog.exe")
            shutil.copyfile(watchdog_src, watchdog_dst)

            # Register in Windows Task Scheduler under SYSTEM with Dual Triggers (Startup + Logon) & Battery Support
            ps_register = (
                f"$action = New-ScheduledTaskAction -Execute '{watchdog_dst}'; "
                "$t1 = New-ScheduledTaskTrigger -AtStartup; "
                "$t2 = New-ScheduledTaskTrigger -AtLogOn; "
                "$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries "
                "-ExecutionTimeLimit (New-TimeSpan -Days 365) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1); "
                "Register-ScheduledTask -TaskName 'SynapseWatchdog' -Action $action -Trigger @($t1, $t2) "
                "-Settings $settings -User 'SYSTEM' -RunLevel Highest -Force"
            )
            subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_register], capture_output=True)
            subprocess.Popen([watchdog_dst], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

    # 5b. Anti-Tamper & Anti-Deletion: Deny delete/write to Users
    try:
        subprocess.run(["icacls", INSTALL_DIR, "/grant", "Users:(OI)(CI)(RX)", "/deny", "Users:(DE,WDAC,WD,AD)"], capture_output=True)
        subprocess.run(["icacls", r"C:\ProgramData\DigiAgent", "/grant", "Users:(OI)(CI)(RX)", "/deny", "Users:(DE,WDAC,WD,AD)"], capture_output=True)
    except Exception:
        pass

    # 6. Service Hardening & Auto-Recovery
    try:
        subprocess.run(["sc", "config", "DigiAgentService", "start=", "auto"], capture_output=True)
        subprocess.run(["sc", "failure", "DigiAgentService", "reset=", "0", "actions=", "restart/1000/restart/1000/restart/1000"], capture_output=True)
        subprocess.run(["sc", "start", "DigiAgentService"], capture_output=True)
    except Exception:
        pass

    # 7. LOGON POLICY: Only show 'Other user' and 'Switch user' on domain-joined computers
    try:
        proc = subprocess.run(
            ["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_ComputerSystem).PartOfDomain"],
            capture_output=True, text=True, timeout=5, creationflags=0x08000000
        )
        is_domain = "true" in proc.stdout.strip().lower()
        dont_display = "1" if is_domain else "0"
        hide_switch = "0" if is_domain else "1"

        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System",
             "/v", "DontDisplayLastUserName", "/t", "REG_DWORD", "/d", dont_display, "/f"],
            capture_output=True
        )
        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System",
             "/v", "HideFastUserSwitching", "/t", "REG_DWORD", "/d", hide_switch, "/f"],
            capture_output=True
        )
        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Policies\Microsoft\Windows\System",
             "/v", "HideFastUserSwitching", "/t", "REG_DWORD", "/d", hide_switch, "/f"],
            capture_output=True
        )
    except Exception:
        pass

    return True

if __name__ == "__main__":
    if not is_admin():
        try:
            ctypes.windll.shell32.ShellExecuteW(
                None, "runas", sys.executable, " ".join(f'"{a}"' for a in sys.argv), None, 1
            )
            sys.exit(0)
        except Exception:
            root = tk.Tk()
            root.withdraw()
            messagebox.showerror("Administrator Required", "Please right-click and select 'Run as administrator'.")
            sys.exit(1)

    root = tk.Tk()
    root.withdraw()

    confirm = messagebox.askyesno(
        "Synapse Classroom Client Setup",
        "Install / Update Hardened Synapse Client on this laptop?\n\n"
        "• Automatic connection to DIGIHUB Classroom\n"
        "• Wi-Fi Disconnection Lockdown Protection\n"
        "• Anti-Tamper Security (Locked against unauthorized removal)\n\n"
        "Click YES to install now."
    )
    if not confirm:
        sys.exit(0)

    ok = run_installation()
    if ok:
        restart = messagebox.askyesno(
            "Installation Complete",
            "✅ Synapse Client Installed Successfully!\n\n"
            "• Connected to DIGIHUB Classroom\n"
            "• Anti-Uninstall Protection Active\n"
            "• Wi-Fi Protection Active\n"
            "• 'Other User' & 'Switch User' Disabled\n\n"
            "NOTE: Windows requires a Restart (or Sign Out) for the lock screen to update.\n\n"
            "Would you like to restart your computer now to complete setup?"
        )
        if restart:
            subprocess.run(["shutdown", "/r", "/t", "5"], capture_output=True)
    else:
        messagebox.showwarning("Setup Warning", "Installation completed with warnings. Please contact your instructor.")
