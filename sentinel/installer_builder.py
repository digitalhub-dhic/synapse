"""
Synapse BYOD Client Installer (legacy build entry point)
Managed Client:
1. Elevates to Administrator automatically (UAC).
2. Installs core Synapse Client silently.
3. Deploys public authentication key.
4. Preserves normal Windows uninstall visibility.
5. SYSTEM watchdog enforces the disclosed classroom policy:
   - If Wi-Fi is disconnected during class, immediately locks screen with [Connect to Wi-Fi].
   - Auto-unlocks when connected.
6. Configures auto-restart recovery on DigiAgentService.
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
    watchdog_src = os.path.join(bundle_dir, "SynapseWatchdog.exe")

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

    # 2. Deploy Authentication Keys
    try:
        os.makedirs(KEY_DIR, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(KEY_DIR, "key"))
        veyon_key_dir = r"C:\ProgramData\Veyon\keys\public\Digihub"
        os.makedirs(veyon_key_dir, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(veyon_key_dir, "key"))
    except Exception:
        pass

    # Preserve the app's uninstall records and visible startup entries.

    # 3. DEPLOY SYSTEM-LEVEL WATCHDOG
    try:
        watchdog_dst = os.path.join(INSTALL_DIR, "SynapseWatchdog.exe")
        shutil.copyfile(watchdog_src, watchdog_dst)

        # Register in Windows Task Scheduler under SYSTEM with highest privileges
        schtasks_cmd = [
            "schtasks", "/Create", "/TN", "SynapseWatchdog",
            "/TR", f'"{watchdog_dst}"',
            "/SC", "ONLOGON", "/RU", "SYSTEM", "/RL", "HIGHEST", "/F"
        ]
        subprocess.run(schtasks_cmd, capture_output=True)

        # Also launch immediately in current desktop session
        subprocess.Popen([watchdog_dst], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

    # 4. Service Hardening & Auto-Recovery
    try:
        subprocess.run(["sc", "config", "DigiAgentService", "start=", "auto"], capture_output=True)
        subprocess.run(["sc", "failure", "DigiAgentService", "reset=", "0", "actions=", "restart/1000/restart/1000/restart/1000"], capture_output=True)
        subprocess.run(["sc", "start", "DigiAgentService"], capture_output=True)
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
        "Install the college Synapse BYOD client on this laptop?\n\n"
        "While enrolled, college staff can monitor the screen and use classroom controls. "
        "The teacher console shows connection and release request status.\n\n"
        "• The SYSTEM watchdog enforces the disclosed classroom policy.\n"
        "• Synapse remains visible in Windows Settings and its uninstall record is preserved.\n\n"
        "Click YES to install now."
    )
    if not confirm:
        sys.exit(0)

    ok = run_installation()
    if ok:
        messagebox.showinfo(
            "Installation Complete",
            "✅ Synapse BYOD Client Installed Successfully!\n\n"
            "• Monitoring request and device status are available in the teacher console.\n"
            "• Wi-Fi disconnection protection is active.\n"
            "• Synapse remains listed in Windows Settings."
        )
    else:
        messagebox.showwarning("Setup Warning", "Installation completed with warnings. Please contact your instructor.")
