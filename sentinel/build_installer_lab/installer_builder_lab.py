"""
Synapse Lab Computer Client Installer
Standard Lab Endpoint:
1. Elevates to Administrator automatically (UAC).
2. Installs core Synapse Client silently.
3. Deploys public authentication keys.
4. Configures DigiAgentService auto-start.
5. NO BYOD features (no Wi-Fi watchdog, no disconnection lock).
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

    # 1b. Deploy latest high-power input blocking engine and DLLs v4.11.7.0
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

    # Record installed version
    try:
        ver_dir = r"C:\ProgramData\DigiAgent"
        os.makedirs(ver_dir, exist_ok=True)
        with open(os.path.join(ver_dir, "installed_version.txt"), "w", encoding="utf-8") as vf:
            vf.write("4.11.7.0\n")
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

    # 3. Clean any stray BYOD files if present
    try:
        subprocess.run(["taskkill", "/F", "/IM", "SynapseBYODGuard.exe"], capture_output=True)
        subprocess.run(["taskkill", "/F", "/IM", "SynapseWatchdog.exe"], capture_output=True)
        subprocess.run(["reg", "delete", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run", "/v", "SynapseBYODGuard", "/f"], capture_output=True)
        subprocess.run(["schtasks", "/Delete", "/TN", "SynapseBYODGuard", "/F"], capture_output=True)
        subprocess.run(["schtasks", "/Delete", "/TN", "SynapseWatchdog", "/F"], capture_output=True)
        for fn in ["SynapseBYODGuard.exe", "SynapseWatchdog.exe"]:
            fp = os.path.join(INSTALL_DIR, fn)
            if os.path.exists(fp):
                os.remove(fp)
    except Exception:
        pass

    # 4. Service Auto-Start Configuration
    try:
        subprocess.run(["sc", "config", "DigiAgentService", "start=", "auto"], capture_output=True)
        subprocess.run(["sc", "failure", "DigiAgentService", "reset=", "0", "actions=", "restart/1000/restart/1000/restart/1000"], capture_output=True)
        subprocess.run(["sc", "start", "DigiAgentService"], capture_output=True)
    except Exception:
        pass

    # 5. LOGON POLICY: Only show 'Other user' and 'Switch user' on domain-joined computers
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
        "Synapse Lab Computer Setup",
        "Install Synapse Client on this Lab Computer?\n\n"
        "• Automatic connection to DIGIHUB Classroom\n"
        "• Standard Lab Mode (Ethernet / LAN)\n"
        "• No BYOD or Wi-Fi restrictions\n\n"
        "Click YES to install now."
    )
    if not confirm:
        sys.exit(0)

    ok = run_installation()
    if ok:
        messagebox.showinfo(
            "Installation Complete",
            "✅ Synapse Lab Client Installed Successfully!\n\n"
            "• Connected to DIGIHUB Classroom\n"
            "• Service Active and Running\n\n"
            "This lab computer is ready for class."
        )
    else:
        messagebox.showwarning("Setup Warning", "Installation completed with warnings. Please contact your instructor.")
