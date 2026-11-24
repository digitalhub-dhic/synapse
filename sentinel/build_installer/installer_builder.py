"""
Synapse Hardened BYOD Client Installer
Managed BYOD Client:
1. Elevates to Administrator automatically (UAC).
2. Installs core Synapse Client silently.
3. Deploys public authentication key.
4. BYOD WI-FI ENFORCEMENT GUARD:
   - Runs in the user session with HIGHEST elevation.
   - Runs at logon under the enrolled user's session.
   - If Wi-Fi is disconnected or switched to unauthorized network, immediately locks screen.
   - Displays full-screen lockdown overlay with [Connect to Wi-Fi] and [Teacher Unlock].
   - Auto-unlocks the instant Wi-Fi reconnects.
5. Service Hardening & Auto-Recovery.
"""

import os
import sys
import time
import datetime
import secrets
import re
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
    watchdog_src = os.path.join(bundle_dir, "SynapseWatchdog.exe")
    tailscale_src = os.path.join(bundle_dir, "tailscale.msi")
    core_dll = os.path.join(bundle_dir, "DigiAgentCore.dll")
    plugin_files = ("windows-platform.dll", "remoteaccess.dll", "screenlock.dll")
    required_payloads = (setup_src, key_src, guard_src, watchdog_src, tailscale_src, core_dll)
    required_payloads += tuple(os.path.join(bundle_dir, name) for name in plugin_files)
    missing = [path for path in required_payloads if not os.path.isfile(path)]
    if missing:
        messagebox.showerror("Setup Error", "Required BYOD installer files are missing:\n\n" + "\n".join(missing))
        return False

    # Stop the previous guard/watchdog before migrating its permissions.
    subprocess.run(["schtasks", "/End", "/TN", "SynapseWatchdog"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "SynapseWatchdog.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "SynapseBYODGuard.exe"], capture_output=True)

    os.makedirs(INSTALL_DIR, exist_ok=True)
    # Migrate from the previous Users deny ACE before changing/removing existing
    # installation files. Administrators belong to Users, so that old ACE also
    # blocked administrative maintenance.
    for protected_dir in (INSTALL_DIR, r"C:\ProgramData\DigiAgent"):
        if os.path.isdir(protected_dir):
            acl_repair = subprocess.run(
                ["icacls", protected_dir, "/T", "/C", "/remove:d", "Users"],
                capture_output=True
            )
            if acl_repair.returncode != 0:
                messagebox.showerror("Setup Error", f"Could not repair permissions from the previous BYOD installation: {protected_dir}")
                return False
    consent_token_path = os.path.join(INSTALL_DIR, "byod-consent-token.txt")
    try:
        os.remove(consent_token_path)
    except FileNotFoundError:
        pass

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
        messagebox.showerror("Setup Error", "The DigiAgent service did not appear after installation.")
        return False

    tailscale_result = subprocess.run(
        ["msiexec.exe", "/i", tailscale_src, "/norestart"],
        capture_output=True, timeout=180
    )
    if tailscale_result.returncode not in (0, 3010):
        messagebox.showerror("Setup Error", f"Tailscale installation failed (exit code {tailscale_result.returncode}).")
        return False
    subprocess.run(["reg", "add", r"HKLM\SOFTWARE\Policies\Tailscale", "/v", "UnattendedMode", "/t", "REG_SZ", "/d", "always", "/f"], check=True, capture_output=True)
    tailscale_service = subprocess.run(["sc", "start", "Tailscale"], capture_output=True, text=True)
    if tailscale_service.returncode != 0 and "1056" not in tailscale_service.stdout:
        messagebox.showerror("Setup Error", "The Tailscale service could not be started.")
        return False

    stop_result = subprocess.run(["sc", "stop", "DigiAgentService"], capture_output=True, text=True)
    if stop_result.returncode != 0 and "1062" not in (stop_result.stdout + stop_result.stderr):
        messagebox.showerror("Setup Error", "Could not stop DigiAgentService before replacing its runtime files.")
        return False
    for _ in range(20):
        service_state = subprocess.run(["sc", "query", "DigiAgentService"], capture_output=True, text=True)
        if re.search(r"STATE\s*:\s*1(?:\s|$)", service_state.stdout):
            break
        time.sleep(0.5)
    else:
        messagebox.showerror("Setup Error", "DigiAgentService did not stop in time; runtime files were not replaced.")
        return False

    # 1b. Deploy the required runtime and control plugins.
    try:
        shutil.copyfile(core_dll, os.path.join(INSTALL_DIR, "DigiAgentCore.dll"))
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not install the classroom runtime: {e}")
        return False
    plugins_dst = os.path.join(INSTALL_DIR, "plugins")
    os.makedirs(plugins_dst, exist_ok=True)
    try:
        for p_dll in plugin_files:
            shutil.copyfile(os.path.join(bundle_dir, p_dll), os.path.join(plugins_dst, p_dll))
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not install required classroom control plugins: {e}")
        return False

    # 2. Deploy Authentication Keys
    try:
        os.makedirs(KEY_DIR, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(KEY_DIR, "key"))
        veyon_key_dir = r"C:\ProgramData\Veyon\keys\public\Digihub"
        os.makedirs(veyon_key_dir, exist_ok=True)
        shutil.copyfile(key_src, os.path.join(veyon_key_dir, "key"))
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not install the classroom authentication key: {e}")
        return False

    # Preserve normal Windows uninstall records. BYOD monitoring is disclosed
    # and enrollment policy must not make the client software invisible.

    # 3. CLEAN OLD STARTUP TOGGLES
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

    # 4. DEPLOY AUTO-HEALING WATCHDOG & GUARD
    try:
        watchdog_dst = os.path.join(INSTALL_DIR, "SynapseWatchdog.exe")
        shutil.copyfile(watchdog_src, watchdog_dst)
        guard_dst = os.path.join(INSTALL_DIR, "SynapseBYODGuard.exe")
        shutil.copyfile(guard_src, guard_dst)

        # Register the watchdog as SYSTEM; keep the UI Guard in the user's session.
        ps_register = (
            f"$action = New-ScheduledTaskAction -Execute '{watchdog_dst}'; "
            "$t1 = New-ScheduledTaskTrigger -AtStartup; "
            "$t2 = New-ScheduledTaskTrigger -AtLogOn; "
            "$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries "
            "-ExecutionTimeLimit (New-TimeSpan -Days 365) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1); "
            "Register-ScheduledTask -TaskName 'SynapseWatchdog' -Action $action -Trigger @($t1, $t2) "
            "-Settings $settings -User 'SYSTEM' -RunLevel Highest -Force"
        )
        task_result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_register], capture_output=True)
        if task_result.returncode != 0:
            raise RuntimeError("Could not register the Synapse watchdog task.")
        subprocess.Popen([watchdog_dst], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        reg_result = subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run",
             "/v", "SynapseBYODGuard", "/t", "REG_SZ", "/d", f'"{guard_dst}"', "/f"],
            capture_output=True
        )
        if reg_result.returncode != 0:
            raise RuntimeError("Could not register the visible BYOD Guard startup entry.")
        subprocess.Popen([guard_dst], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not install required BYOD monitoring components: {e}")
        return False

    # 4b. Protect service files from standard-user tampering
    try:
        data_dir = r"C:\ProgramData\DigiAgent"
        os.makedirs(data_dir, exist_ok=True)
        guard_log = os.path.join(data_dir, "guard.log")
        with open(guard_log, "a", encoding="utf-8"):
            pass
        for protected_dir in (INSTALL_DIR, data_dir):
            acl_result = subprocess.run(
                ["icacls", protected_dir, "/inheritance:r", "/T", "/C", "/remove:d", "Users", "/grant:r",
                 "SYSTEM:(OI)(CI)(F)", "Administrators:(OI)(CI)(F)", "Users:(OI)(CI)(RX)"],
                capture_output=True
            )
            if acl_result.returncode != 0:
                raise RuntimeError(f"Could not secure permissions for {protected_dir}.")
        log_acl = subprocess.run(
            ["icacls", guard_log, "/grant:r", "SYSTEM:(F)", "Administrators:(F)", "Users:(W)"],
            capture_output=True
        )
        if log_acl.returncode != 0:
            raise RuntimeError("Could not grant the BYOD Guard access to its log file.")
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not secure BYOD installation permissions: {e}")
        return False

    # 5. Service Hardening & Auto-Recovery
    try:
        subprocess.run(["sc", "config", "DigiAgentService", "start=", "auto"], capture_output=True, check=True)
        subprocess.run(["sc", "failure", "DigiAgentService", "reset=", "0", "actions=", "restart/1000/restart/1000/restart/1000"], capture_output=True, check=True)
        service_result = subprocess.run(["sc", "start", "DigiAgentService"], capture_output=True, text=True)
        if service_result.returncode != 0 and "1056" not in service_result.stdout:
            raise RuntimeError("Could not start DigiAgentService.")
    except Exception as e:
        messagebox.showerror("Setup Error", f"Could not configure the required classroom service: {e}")
        return False

    # 6. LOGON POLICY: Only show 'Other user' and 'Switch user' on domain-joined computers
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

    nonce = os.environ.get("SYNAPSE_CONSENT_NONCE") or secrets.token_urlsafe(24)
    with open(os.path.join(INSTALL_DIR, "byod-consent.ini"), "a", encoding="utf-8") as f:
        f.write(
            f"accepted_at={datetime.datetime.now().astimezone().isoformat()} "
            f"installer_windows_account={os.environ.get('USERDOMAIN', '')}\\{os.environ.get('USERNAME', '')}\n"
        )
    with open(consent_token_path, "w", encoding="ascii") as f:
        f.write(nonce)
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
        "Install or update the college Synapse BYOD client on this laptop?\n\n"
        "While enrolled and connected to the classroom system, authorized staff can view the screen, take screenshots, lock or unlock it, send messages, and use supported app, power/session, and file controls.\n\n"
        "• Tailscale is installed visibly for college network access; it must be authenticated to the college tailnet.\n"
        "• A visible Synapse Guard can lock the session if its approved classroom connection is lost.\n"
        "• Release and re-enforcement requests may be unconfirmed; ask the instructor to verify device status.\n"
        "• Standard Windows uninstall records remain available.\n\n"
        "Click YES only if you understand and accept classroom monitoring and these controls. Click NO to cancel BYOD enrollment."
    )
    if not confirm:
        sys.exit(0)

    try:
        ok = run_installation()
    except Exception as e:
        messagebox.showerror("Setup Error", f"BYOD installation failed and enrollment was not completed.\n\n{e}")
        sys.exit(1)
    if ok:
        messagebox.showinfo(
            "Installation Complete",
            "Synapse components are installed. Return to the setup window and complete teacher password and updater setup before restarting.\n\n"
            "• Synapse and Tailscale are visible in Windows Settings.\n"
            "• Monitoring and release request status appears in the teacher console.\n"
            "• Wi-Fi disconnection protection is active.\n"
            "• 'Other User' and 'Switch User' restrictions may apply under college policy."
        )
    else:
        sys.exit(1)
