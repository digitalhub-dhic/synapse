"""Windows BYOD connectivity watchdog.

This process is installed as SYSTEM to attempt classroom network recovery.
Interactive lock UI belongs in the logged-in student's session; a SYSTEM task
cannot reliably display UI across Windows session isolation.
"""

import os
import sys
import time
import socket
import ctypes
import subprocess
import threading

# Single-Instance Enforcement: Prevents duplicate watchdog processes
_MUTEX_NAME = "Global\\SynapseWatchdog_SingleInstance_Mutex"
_mutex_handle = ctypes.windll.kernel32.CreateMutexW(None, False, _MUTEX_NAME)
if ctypes.windll.kernel32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
    sys.exit(0)

CONFIG_FILE = r"C:\ProgramData\DigiAgent\byod_policy.json"
INSTALL_DIR = r"C:\Program Files\DigiAgent"

# The teacher password is provisioned per installation. Never embed a shared
# password or verifier in distributable client code.
import hmac
import hashlib
CREDENTIAL_FILE = r"C:\ProgramData\DigiAgent\teacher_credential.json"
PASSWORD_ITERATIONS = 310_000

def verify_master_password(candidate: str) -> bool:
    if not candidate:
        return False
    try:
        import base64
        import json
        with open(CREDENTIAL_FILE, "r", encoding="utf-8") as credential_file:
            credential = json.load(credential_file)
        salt = base64.b64decode(credential["salt"], validate=True)
        expected = base64.b64decode(credential["hash"], validate=True)
        iterations = int(credential.get("iterations", PASSWORD_ITERATIONS))
        if len(salt) < 16 or len(expected) != 32 or iterations < 100_000 or iterations > 2_000_000:
            return False
        actual = hashlib.pbkdf2_hmac("sha256", candidate.encode("utf-8"), salt, iterations)
        return hmac.compare_digest(actual, expected)
    except (OSError, ValueError, TypeError, KeyError, ImportError):
        return False

# Auto-heal execution gate: Prevents thread fork-bombs and CPU spikes
_heal_lock = threading.Lock()
_healing_in_progress = False

def trigger_auto_heal_async():
    global _healing_in_progress
    with _heal_lock:
        if _healing_in_progress:
            return
        _healing_in_progress = True

    def _worker():
        global _healing_in_progress
        try:
            auto_heal_network()
        finally:
            with _heal_lock:
                _healing_in_progress = False

    threading.Thread(target=_worker, daemon=True).start()

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
        ("fileserver.local", 445),
        ("computer-50.local", 445),
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

def enforce_anti_tamper_permissions():
    """Protect client files while preserving administrator access and guard logging."""
    try:
        if os.path.exists(INSTALL_DIR):
            subprocess.run(
                ["icacls", INSTALL_DIR, "/inheritance:r", "/T", "/C", "/remove:d", "Users", "/grant:r",
                 "SYSTEM:(OI)(CI)(F)", "Administrators:(OI)(CI)(F)", "Users:(OI)(CI)(RX)"],
                capture_output=True, timeout=3, creationflags=0x08000000, check=True
            )
        data_dir = r"C:\ProgramData\DigiAgent"
        if os.path.exists(data_dir):
            subprocess.run(
                ["icacls", data_dir, "/inheritance:r", "/T", "/C", "/remove:d", "Users", "/grant:r",
                 "SYSTEM:(OI)(CI)(F)", "Administrators:(OI)(CI)(F)", "Users:(OI)(CI)(RX)"],
                capture_output=True, timeout=3, creationflags=0x08000000, check=True
            )
            # The interactive Guard runs as the logged-in student. Give it
            # append/write permission only to its non-sensitive diagnostic log.
            guard_log = os.path.join(data_dir, "guard.log")
            with open(guard_log, "a", encoding="utf-8"):
                pass
            subprocess.run(
                ["icacls", guard_log, "/grant:r", "SYSTEM:(F)", "Administrators:(F)", "Users:(W)"],
                capture_output=True, timeout=3, creationflags=0x08000000, check=True
            )
    except Exception as exc:
        log_debug(f"Could not secure BYOD file permissions: {exc}")


def watchdog_worker():
    enforce_anti_tamper_permissions()

    time.sleep(3)  # Brief grace for initial network stabilization
    consecutive_offline = 0

    while True:
        try:
            # 1. Check if teacher released device
            if is_teacher_released():
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
                else:
                    log_debug("Network recovery did not restore classroom connectivity; the student-session Guard handles the lock screen.")
            else:
                consecutive_offline = 0

            time.sleep(2)

        except Exception as e:
            log_debug(f"Watchdog exception: {e}")
            time.sleep(2)

if __name__ == "__main__":
    watchdog_worker()
