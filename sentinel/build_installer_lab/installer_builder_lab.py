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
    runtime_files = (
        "DigiAgentCore.dll",
        os.path.join("plugins", "windows-platform.dll"),
        os.path.join("plugins", "remoteaccess.dll"),
        os.path.join("plugins", "screenlock.dll"),
    )
    required = (setup_src, key_src) + tuple(os.path.join(bundle_dir, path) for path in runtime_files)
    missing = [path for path in required if not os.path.isfile(path)]
    if missing:
        messagebox.showerror("Setup Error", "Required lab installer files are missing:\n\n" + "\n".join(missing))
        return False

    temp_dir = r"C:\Windows\Temp"
    os.makedirs(temp_dir, exist_ok=True)
    temp_setup = os.path.join(temp_dir, "Synapse-Core-Setup.exe")

    # 1. Silently install core Synapse client
    try:
        shutil.copyfile(setup_src, temp_setup)
        core_result = subprocess.run([temp_setup, "/S"], capture_output=True, timeout=120)
        if core_result.returncode != 0:
            raise RuntimeError(f"Core installer exited with code {core_result.returncode}.")
    except Exception as e:
        messagebox.showerror("Setup Error", f"Core installation failed: {e}")
        return False

    # Wait for files to unpack
    for _ in range(25):
        if os.path.exists(os.path.join(INSTALL_DIR, "digiagent-service.exe")):
            break
        time.sleep(1)
    else:
        messagebox.showerror("Setup Error", "The DigiAgent service executable did not appear after installation.")
        return False

    # Stop the service before replacing DLLs that it may currently have loaded.
    try:
        status = subprocess.run(["sc", "query", "DigiAgentService"], capture_output=True, text=True, check=True)
        if "RUNNING" in status.stdout:
            subprocess.run(["sc", "stop", "DigiAgentService"], capture_output=True, text=True, check=True)
        for _ in range(30):
            status = subprocess.run(["sc", "query", "DigiAgentService"], capture_output=True, text=True, check=True)
            if "STOPPED" in status.stdout:
                break
            time.sleep(1)
        else:
            raise RuntimeError("DigiAgentService did not stop in time; runtime files were not replaced.")
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not stop DigiAgentService before replacing runtime files: {e}")
        return False

    # 1b. Deploy latest high-power input blocking engine and DLLs v4.11.7.0
    plugins_dst = os.path.join(INSTALL_DIR, "plugins")
    os.makedirs(plugins_dst, exist_ok=True)
    try:
        shutil.copyfile(os.path.join(bundle_dir, "DigiAgentCore.dll"), os.path.join(INSTALL_DIR, "DigiAgentCore.dll"))
        for plugin in runtime_files[1:]:
            shutil.copyfile(os.path.join(bundle_dir, plugin), os.path.join(plugins_dst, os.path.basename(plugin)))
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not install the required classroom runtime: {e}")
        return False

    # 2. Deploy Authentication Keys
    try:
        os.makedirs(KEY_DIR, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(KEY_DIR, "key"))
        veyon_key_dir = r"C:\ProgramData\Veyon\keys\public\Digihub"
        os.makedirs(veyon_key_dir, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(veyon_key_dir, "key"))
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not install the classroom public key: {e}")
        return False

    # 3. Remove any BYOD enforcement task/process before claiming this is a
    # standard lab endpoint with no BYOD lock behavior.
    try:
        cleanup_ps = r"""
$ErrorActionPreference = 'Stop'
foreach ($name in @('SynapseBYODGuard', 'SynapseWatchdog')) {
    Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction Stop
    if (Get-Process -Name $name -ErrorAction SilentlyContinue) { throw "Process $name is still running." }
    $task = Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
    if ($task) { Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction Stop }
    if (Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue) { throw "Scheduled task $name still exists." }
}
Remove-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run' -Name 'SynapseBYODGuard' -ErrorAction SilentlyContinue
Remove-ItemProperty -Path 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run' -Name 'SynapseBYODGuard' -ErrorAction SilentlyContinue
$startupDirs = @(
    "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\Startup",
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup"
)
$startupDirs += Get-ChildItem 'C:\Users' -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    Join-Path $_.FullName 'AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup'
}
foreach ($startupDir in $startupDirs) {
    foreach ($file in @('SynapseBYODGuard.lnk', 'SynapseBYODGuard.bat')) {
        $path = Join-Path $startupDir $file
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force -ErrorAction Stop }
        if (Test-Path -LiteralPath $path) { throw "Startup entry $path still exists." }
    }
}
$byodDataDir = 'C:\ProgramData\DigiAgent'
foreach ($file in @('teacher_credential.json', 'byod_policy.json', 'kiosk_locked.state', 'byod-consent-token.txt')) {
    $path = Join-Path $byodDataDir $file
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force -ErrorAction Stop }
    if (Test-Path -LiteralPath $path) { throw "BYOD state file $path still exists." }
}
"""
        subprocess.run(["powershell", "-NoProfile", "-Command", cleanup_ps], capture_output=True, text=True, check=True)
        for fn in ["SynapseBYODGuard.exe", "SynapseWatchdog.exe"]:
            fp = os.path.join(INSTALL_DIR, fn)
            if os.path.exists(fp):
                os.remove(fp)
                if os.path.exists(fp):
                    raise RuntimeError(f"Could not remove legacy BYOD component: {fp}")
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not remove BYOD enforcement from this lab client: {e}")
        return False

    # 4. Service Auto-Start Configuration
    try:
        subprocess.run(["sc", "config", "DigiAgentService", "start=", "auto"], capture_output=True, check=True)
        subprocess.run(["sc", "failure", "DigiAgentService", "reset=", "0", "actions=", "restart/1000/restart/1000/restart/1000"], capture_output=True, check=True)
        service_result = subprocess.run(["sc", "start", "DigiAgentService"], capture_output=True, text=True)
        if service_result.returncode != 0 and "1056" not in (service_result.stdout + service_result.stderr):
            raise RuntimeError(f"DigiAgentService failed to start: {(service_result.stderr or service_result.stdout).strip()}")
        for _ in range(30):
            status = subprocess.run(["sc", "query", "DigiAgentService"], capture_output=True, text=True, check=True)
            if "RUNNING" in status.stdout:
                break
            time.sleep(1)
        else:
            raise RuntimeError("DigiAgentService did not reach the running state.")
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not configure and verify the classroom service: {e}")
        return False

    # 5. LOGON POLICY: Only show 'Other user' and 'Switch user' on domain-joined computers
    try:
        proc = subprocess.run(
            ["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_ComputerSystem).PartOfDomain"],
            capture_output=True, text=True, timeout=5, creationflags=0x08000000
        )
        if proc.returncode != 0 or proc.stdout.strip().lower() not in ("true", "false"):
            raise RuntimeError("Could not determine whether this lab computer is domain joined.")
        is_domain = "true" in proc.stdout.strip().lower()
        dont_display = "1" if is_domain else "0"
        hide_switch = "0" if is_domain else "1"

        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System",
             "/v", "DontDisplayLastUserName", "/t", "REG_DWORD", "/d", dont_display, "/f"],
            capture_output=True, check=True
        )
        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System",
             "/v", "HideFastUserSwitching", "/t", "REG_DWORD", "/d", hide_switch, "/f"],
            capture_output=True, check=True
        )
        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Policies\Microsoft\Windows\System",
             "/v", "HideFastUserSwitching", "/t", "REG_DWORD", "/d", hide_switch, "/f"],
            capture_output=True, check=True
        )
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not apply lab logon policy: {e}")
        return False

    # Secure the packaged install even when launched without the batch wrapper.
    try:
        acl_args = [
            "/inheritance:r", "/T", "/C", "/remove:d", "*S-1-5-32-545", "/grant:r",
            "*S-1-5-18:(OI)(CI)(F)", "*S-1-5-32-544:(OI)(CI)(F)", "*S-1-5-32-545:(OI)(CI)(RX)",
        ]
        for protected_dir in (INSTALL_DIR, r"C:\ProgramData\DigiAgent"):
            if os.path.isdir(protected_dir):
                subprocess.run(["icacls", protected_dir, *acl_args], capture_output=True, check=True)
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not secure lab client files while preserving administrator access: {e}")
        return False

    # Commit installed_version only after all required runtime, key, service,
    # and logon-policy steps have succeeded.
    try:
        ver_dir = r"C:\ProgramData\DigiAgent"
        os.makedirs(ver_dir, exist_ok=True)
        version_tmp = os.path.join(ver_dir, "installed_version.txt.tmp")
        with open(version_tmp, "w", encoding="ascii") as vf:
            vf.write("4.11.7.0\n")
        os.replace(version_tmp, os.path.join(ver_dir, "installed_version.txt"))
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not commit the installed client version: {e}")
        return False

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
            "Synapse Lab Client installation completed.\n\n"
            "• Required runtime and classroom key installed\n"
            "• DigiAgentService verified running\n"
            "• Lab logon policy applied\n\n"
            "Confirm live monitoring from the teacher console before class."
        )
    else:
        messagebox.showwarning("Setup Warning", "Installation completed with warnings. Please contact your instructor.")
