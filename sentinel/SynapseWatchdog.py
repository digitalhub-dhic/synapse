"""
Synapse Hardened BYOD Watchdog v4.0 (Zero-Bypass & Reboot-Persistent Edition)
Key Security Enforcement:
1. When Wi-Fi is disconnected or Airplane Mode is enabled:
   - Does NOT immediately lock into kiosk mode.
   - Actively auto-heals: turns OFF Airplane Mode, re-enables Wi-Fi adapters, and auto-reconnects to school Wi-Fi.
2. If auto-reconnection succeeds:
   - Student continues working seamlessly with ZERO kiosk interruption.
3. If auto-reconnection fails:
   - Enforces persistent KIOSK MODE.
4. UNBREAKABLE RULES:
   - Can ONLY be overcome by:
     a) Entering the Teacher Master Password ("Synapse#Master2026!").
     b) Reconnecting to the authorized Classroom Wi-Fi / Teacher Server.
     c) Teacher Remote Release (Teacher-Release-BYOD.bat).
   - CANNOT be overcome without password or classroom Wi-Fi.
   - CANNOT be overcome by signing out, signing in, fast user switching, or restarting the PC.
   - Persistent lock state saved to disk; immediately re-locks on boot/logon if still offline.
5. Anti-Tamper & Anti-Deletion:
   - Runs as SYSTEM with highest privileges across boot and logon.
   - NTFS ACL explicitly denies Delete/Modify to standard Users.
   - Task Manager blocked during lockout.
   - Fast user switching disabled in Windows.
"""

import os
import sys
import time
import socket
import ctypes
import subprocess
import threading
import tkinter as tk
from tkinter import simpledialog, messagebox

# Single-Instance Enforcement: Prevents duplicate watchdog processes
_MUTEX_NAME = "Global\\SynapseWatchdog_SingleInstance_Mutex"
_mutex_handle = ctypes.windll.kernel32.CreateMutexW(None, False, _MUTEX_NAME)
if ctypes.windll.kernel32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
    sys.exit(0)

CONFIG_FILE = r"C:\ProgramData\DigiAgent\byod_policy.json"
LOCK_STATE_FILE = r"C:\ProgramData\DigiAgent\kiosk_locked.state"
INSTALL_DIR = r"C:\Program Files\DigiAgent"
MASTER_PASS = "Synapse#Master2026!"

def log_debug(msg):
    try:
        log_dir = r"C:\ProgramData\DigiAgent"
        os.makedirs(log_dir, exist_ok=True)
        with open(os.path.join(log_dir, "watchdog.log"), "a", encoding="utf-8") as f:
            f.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {msg}\n")
    except Exception:
        pass

def is_teacher_released():
    """Checks if the teacher put the computer in released state via byod_policy.json."""
    if os.path.exists(CONFIG_FILE):
        try:
            import json
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                return bool(json.load(f).get("released", False))
        except Exception:
            pass
    return False

def is_state_locked():
    """Checks persistent lock status on disk across reboots."""
    if os.path.exists(LOCK_STATE_FILE):
        try:
            with open(LOCK_STATE_FILE, "r", encoding="utf-8") as f:
                return f.read().strip() == "LOCKED"
        except Exception:
            pass
    return False

def set_state_locked(locked: bool):
    """Saves persistent lock status to disk."""
    try:
        os.makedirs(os.path.dirname(LOCK_STATE_FILE), exist_ok=True)
        with open(LOCK_STATE_FILE, "w", encoding="utf-8") as f:
            f.write("LOCKED" if locked else "UNLOCKED")
    except Exception:
        pass

def is_classroom_connected():
    """
    STRICT VERIFICATION:
    Connecting to arbitrary Wi-Fi (such as a mobile hotspot or home network) will FAIL.
    The workstation is only considered online if it can physically reach the classroom
    infrastructure (Teacher Server 192.168.1.150, Classroom Gateway 192.168.1.1, or Tailscale 100.99.161.43).
    """
    targets = [
        ("192.168.1.150", 445),   # Teacher Server SMB / Host
        ("192.168.1.1", 53),     # Classroom Router DNS
        ("100.99.161.43", 445),  # Teacher Mesh Server
    ]
    for host, port in targets:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.4)
            s.connect((host, port))
            s.close()
            return True
        except Exception:
            pass

    # Secondary ICMP probe to classroom gateway
    try:
        proc = subprocess.run(
            ["ping", "-n", "1", "-w", "350", "192.168.1.1"],
            capture_output=True, timeout=1, creationflags=0x08000000
        )
        if proc.returncode == 0:
            return True
    except Exception:
        pass

    return False

def auto_heal_network():
    """
    Actively heals the network when student turns off Wi-Fi or enables Airplane Mode:
    1. Disables Airplane Mode via SystemRadioState registry.
    2. Forces all wireless network adapters to ENABLED state.
    3. Reconnects to saved school Wi-Fi profiles.
    Returns True if successfully reconnected to Classroom network, False if failed.
    """
    log_debug("Classroom network lost. Initiating auto-heal routine...")

    # Step 1: Force Turn OFF Airplane Mode
    try:
        subprocess.run(
            ["reg", "add", r"HKLM\SYSTEM\CurrentControlSet\Control\RadioManagement\SystemRadioState",
             "/ve", "/t", "REG_DWORD", "/d", "0", "/f"],
            capture_output=True, timeout=2, creationflags=0x08000000
        )
        subprocess.run(["sc", "control", "RmSvc", "paramchange"],
                       capture_output=True, timeout=2, creationflags=0x08000000)
    except Exception as e:
        log_debug(f"Airplane disable error: {e}")

    # Step 2: Force Enable Wi-Fi Adapters
    try:
        subprocess.run(["netsh", "interface", "set", "interface", "name=Wi-Fi", "admin=enabled"],
                       capture_output=True, timeout=3, creationflags=0x08000000)
        subprocess.run(["netsh", "interface", "set", "interface", "name=Wireless Network Connection", "admin=enabled"],
                       capture_output=True, timeout=3, creationflags=0x08000000)
        ps_enable = (
            "Get-NetAdapter | Where-Object { "
            "$_.InterfaceDescription -match 'Wireless|Wi-Fi|802.11' -or $_.Name -match 'Wi-Fi|WLAN' "
            "} | Enable-NetAdapter -Confirm:$false -ErrorAction SilentlyContinue"
        )
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_enable],
                       capture_output=True, timeout=4, creationflags=0x08000000)
    except Exception as e:
        log_debug(f"Adapter enable error: {e}")

    # Step 3: Reconnect to saved Wi-Fi profiles
    try:
        proc = subprocess.run(
            ["netsh", "wlan", "show", "profiles"],
            capture_output=True, text=True, timeout=3, creationflags=0x08000000
        )
        profiles = []
        for line in proc.stdout.splitlines():
            if ":" in line and ("All User Profile" in line or "User Profile" in line):
                prof = line.split(":", 1)[1].strip()
                if prof and prof not in profiles:
                    profiles.append(prof)

        for prof in profiles:
            subprocess.run(
                ["netsh", "wlan", "connect", f"name={prof}"],
                capture_output=True, timeout=3, creationflags=0x08000000
            )
            time.sleep(1.2)
            if is_classroom_connected():
                log_debug(f"Successfully auto-reconnected to classroom network profile: {prof}")
                return True
    except Exception as e:
        log_debug(f"Profile connect error: {e}")

    # Step 4: Verification wait (up to 4 seconds for DHCP handshake)
    for _ in range(4):
        if is_classroom_connected():
            log_debug("Classroom connection verified.")
            return True
        time.sleep(1)

    log_debug("Auto-heal could not reach classroom network.")
    return False

def enforce_system_policies(locked: bool):
    """
    Prevents bypass via:
    1. Task Manager (DisableTaskMgr)
    2. Fast User Switching / Switch User (HideFastUserSwitching)
    """
    val = "1" if locked else "0"
    try:
        # Disable/Enable Task Manager
        subprocess.run(
            ["reg", "add", r"HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\System",
             "/v", "DisableTaskMgr", "/t", "REG_DWORD", "/d", val, "/f"],
            capture_output=True, timeout=2, creationflags=0x08000000
        )
        # Hide Fast User Switching so student cannot switch accounts
        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System",
             "/v", "HideFastUserSwitching", "/t", "REG_DWORD", "/d", "1", "/f"],
            capture_output=True, timeout=2, creationflags=0x08000000
        )
    except Exception:
        pass

def enforce_anti_tamper_permissions():
    """Ensures students cannot delete, rename, or tamper with the watchdog files."""
    try:
        if os.path.exists(INSTALL_DIR):
            subprocess.run(
                ["icacls", INSTALL_DIR, "/grant", "Users:(OI)(CI)(RX)", "/deny", "Users:(DE,WDAC,WD,AD)"],
                capture_output=True, timeout=3, creationflags=0x08000000
            )
        data_dir = r"C:\ProgramData\DigiAgent"
        if os.path.exists(data_dir):
            subprocess.run(
                ["icacls", data_dir, "/grant", "Users:(OI)(CI)(RX)", "/deny", "Users:(DE,WDAC,WD,AD)"],
                capture_output=True, timeout=3, creationflags=0x08000000
            )
    except Exception:
        pass


class HardenedLockUI:
    def __init__(self):
        self.root = None
        self.is_active = False
        self.override = False

    def display(self):
        if self.is_active or self.override or is_teacher_released():
            return
        self.is_active = True
        set_state_locked(True)
        enforce_system_policies(True)

        self.root = tk.Tk()
        self.root.title("Synapse Classroom Lock")
        self.root.attributes("-fullscreen", True)
        self.root.attributes("-topmost", True)
        self.root.configure(bg="#0B132B")

        # Block all shortcuts and close attempts
        self.root.protocol("WM_DELETE_WINDOW", lambda: None)
        self.root.bind("<Alt-F4>", lambda e: "break")
        self.root.bind("<Escape>", lambda e: "break")

        def keep_topmost():
            if self.is_active and self.root:
                try:
                    self.root.lift()
                    self.root.attributes("-topmost", True)
                    self.root.after(300, keep_topmost)
                except Exception:
                    pass

        # Card container
        card = tk.Frame(self.root, bg="#1C2541", padx=55, pady=45, relief=tk.RAISED, bd=2)
        card.place(relx=0.5, rely=0.5, anchor=tk.CENTER)

        lbl_icon = tk.Label(card, text="🛑", font=("Segoe UI", 52), fg="#EF4444", bg="#1C2541")
        lbl_icon.pack(pady=(0, 10))

        lbl_title = tk.Label(card, text="Classroom Enforcement Active", font=("Segoe UI", 22, "bold"), fg="#FFFFFF", bg="#1C2541")
        lbl_title.pack(pady=(0, 12))

        lbl_desc = tk.Label(card,
            text="Disconnecting from school Wi-Fi or enabling Airplane Mode is prohibited.\n\n"
                 "This workstation is locked by instructor policy.\n"
                 "Restarting or signing out will NOT bypass this lock.\n"
                 "Must be unlocked by Teacher Password or reconnecting to Classroom Wi-Fi.",
            font=("Segoe UI", 12), fg="#CBD5E1", bg="#1C2541", justify=tk.CENTER)
        lbl_desc.pack(pady=(0, 25))

        btn_unlock = tk.Button(card, text="🔑  Teacher Unlock", font=("Segoe UI", 12, "bold"),
                               bg="#3A86FF", fg="white", activebackground="#2563EB", activeforeground="white",
                               padx=25, pady=8, relief=tk.FLAT, cursor="hand2", command=self.teacher_override)
        btn_unlock.pack(pady=(5, 10), fill=tk.X)

        lbl_status = tk.Label(card, text="🔒 Persistent Lock • Reboots & Sign-Outs Protected",
                              font=("Segoe UI", 9, "italic"), fg="#94A3B8", bg="#1C2541")
        lbl_status.pack(pady=(5, 0))

        keep_topmost()
        self.poll_network()
        self.root.mainloop()

    def teacher_override(self):
        pwd = simpledialog.askstring("Instructor Unlock", "Enter Teacher Master Password:", parent=self.root, show="*")
        if pwd == MASTER_PASS:
            self.override = True
            set_state_locked(False)
            self.dismiss()
        elif pwd is not None:
            messagebox.showerror("Access Denied", "Incorrect master password.", parent=self.root)

    def poll_network(self):
        # Auto-unlock ONLY if Teacher Released OR Verified Classroom Connection Restored
        if is_teacher_released() or is_classroom_connected():
            set_state_locked(False)
            self.dismiss()
            return

        # Background healing attempt continues running quietly
        threading.Thread(target=auto_heal_network, daemon=True).start()

        if self.root and self.is_active:
            self.root.after(2000, self.poll_network)

    def dismiss(self):
        self.is_active = False
        enforce_system_policies(False)
        if self.root:
            try:
                self.root.destroy()
            except Exception:
                pass
            self.root = None


lock_ui = HardenedLockUI()

def watchdog_worker():
    enforce_anti_tamper_permissions()
    enforce_system_policies(False)

    # REBOOT & SIGN-IN PERSISTENCE CHECK:
    # If computer rebooted or user signed out while in locked state:
    if is_state_locked() and not is_teacher_released() and not is_classroom_connected():
        log_debug("Startup/Logon detected with persistent lock active. Triggering KIOSK immediately!")
        lock_ui.display()

    time.sleep(3)  # Brief grace for initial network stabilization
    consecutive_offline = 0

    while True:
        try:
            # 1. Check if teacher released device
            if is_teacher_released():
                set_state_locked(False)
                if lock_ui.is_active:
                    lock_ui.dismiss()
                time.sleep(5)
                continue

            # 2. Check strict classroom connectivity
            if not is_classroom_connected():
                consecutive_offline += 1
                log_debug(f"Classroom unreachable (check #{consecutive_offline}). Attempting auto-heal...")

                # ATTEMPT AUTO-HEAL: Disable Airplane mode, enable Wi-Fi, auto-reconnect
                healed = auto_heal_network()
                if healed:
                    consecutive_offline = 0
                    set_state_locked(False)
                    if lock_ui.is_active:
                        lock_ui.dismiss()
                else:
                    # If auto-heal cannot reach Classroom Server, IMMEDIATELY enter KIOSK MODE
                    if not lock_ui.is_active:
                        log_debug("Cannot reach classroom network. Triggering persistent KIOSK MODE.")
                        set_state_locked(True)
                        lock_ui.display()
            else:
                consecutive_offline = 0
                lock_ui.override = False
                set_state_locked(False)
                if lock_ui.is_active:
                    lock_ui.dismiss()

            # Periodic permission reinforcement
            enforce_anti_tamper_permissions()
            time.sleep(2)

        except Exception as e:
            log_debug(f"Watchdog exception: {e}")
            time.sleep(2)

if __name__ == "__main__":
    watchdog_worker()
