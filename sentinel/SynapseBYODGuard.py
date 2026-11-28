"""
Synapse BYOD Campus/Personal Mode Controller - Non-Blocking Edition
Replaces aggressive Wi-Fi OS lockdown with Campus Mode vs. Personal Mode switching:
- Campus Mode: When on campus (or forced by policy), ensures Tailscale + DigiAgentService
  supervision are active and reports tailscale_node_id + mode telemetry via UDP beacon.
- Personal Mode: When off-campus / Wi-Fi disconnected, paused, or revoked, stops
  DigiAgentService supervision so the student's personal laptop remains 100% usable
  offline and at home without any OS/keyboard lockdown.
"""

import os
import sys
import json
import time
import hmac
import uuid
import socket
import hashlib
import ctypes
import subprocess
import threading

# Salted SHA-256 hash of Teacher Master Password with constant-time verification
PASSWORD_SALT = b"SynapseEnterpriseSalt2026"
PASSWORD_HASH = "efa0f57bdc9955b979a271903c7ae1c79272c1298df1c17d10c929c30860c33d"

LOG_FILE = r"C:\ProgramData\DigiAgent\guard.log"
CONFIG_FILE = r"C:\ProgramData\DigiAgent\byod_policy.json"
STATE_FILE = r"C:\ProgramData\DigiAgent\byod_mode_state.json"

CLASSROOM_SERVER = "192.168.1.150"
TAILSCALE_SERVER = "100.99.161.43"
BEACON_UDP_PORT = 11105
DIGIAGENT_SERVICE = "DigiAgentService"

MODE_CAMPUS = "campus"
MODE_PERSONAL = "personal"
MODE_PAUSED = "paused"
MODE_REVOKED = "revoked"


def verify_master_password(candidate: str) -> bool:
    if not candidate:
        return False
    cand_hash = hashlib.sha256(PASSWORD_SALT + candidate.encode("utf-8")).hexdigest()
    return hmac.compare_digest(cand_hash, PASSWORD_HASH)


def log(msg: str) -> None:
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    entry = f"[{ts}] {msg}"
    print(entry, flush=True)
    try:
        os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(entry + "\n")
    except Exception:
        pass


def clear_legacy_lock_policies() -> None:
    """Clears any leftover Task Manager / Taskbar registry locks from legacy BYODGuard."""
    try:
        subprocess.run(
            [
                "reg", "add",
                r"HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\System",
                "/v", "DisableTaskMgr", "/t", "REG_DWORD", "/d", "0", "/f",
            ],
            capture_output=True, creationflags=0x08000000, timeout=3,
        )
        subprocess.run(
            [
                "reg", "add",
                r"HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced",
                "/v", "ShowTaskViewButton", "/t", "REG_DWORD", "/d", "1", "/f",
            ],
            capture_output=True, creationflags=0x08000000, timeout=3,
        )
    except Exception as e:
        log(f"clear_legacy_lock_policies warning: {e}")


def load_policy() -> dict:
    """Loads byod_policy.json; supports legacy {'released': true} and pause/revoke/forced_mode."""
    default_policy = {
        "released": False,
        "paused": False,
        "revoked": False,
        "forced_mode": None,  # None | "campus" | "personal"
    }
    if not os.path.exists(CONFIG_FILE):
        return default_policy
    try:
        with open(CONFIG_FILE, "r", encoding="utf-8") as f:
            raw = json.load(f)
        if isinstance(raw, dict):
            default_policy.update(raw)
    except Exception as e:
        log(f"load_policy error: {e}")
    return default_policy


def save_policy(policy: dict) -> None:
    try:
        os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
        tmp = CONFIG_FILE + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(policy, f, indent=2)
        os.replace(tmp, CONFIG_FILE)
    except Exception as e:
        log(f"save_policy error: {e}")


def get_tailscale_identity() -> dict:
    """Returns {'tailscale_node_id': str, 'tailscale_ip': str, 'tailscale_dns': str}."""
    identity = {"tailscale_node_id": "", "tailscale_ip": "", "tailscale_dns": ""}
    try:
        proc = subprocess.run(
            ["tailscale", "status", "--json"],
            capture_output=True, text=True, timeout=3, creationflags=0x08000000,
        )
        if proc.returncode == 0 and proc.stdout.strip():
            data = json.loads(proc.stdout)
            self_node = data.get("Self") or {}
            identity["tailscale_node_id"] = str(self_node.get("ID") or "")
            identity["tailscale_dns"] = str(self_node.get("DNSName") or "").rstrip(".")
            ips = self_node.get("TailscaleIPs") or []
            if ips:
                identity["tailscale_ip"] = str(ips[0])
    except Exception:
        pass
    return identity


def get_primary_mac() -> str:
    node = uuid.getnode()
    if (node >> 40) & 0x01:
        return ""
    return ":".join(f"{(node >> ele) & 0xFF:02X}" for ele in range(40, -1, -8))


def get_local_lan_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.5)
        s.connect((CLASSROOM_SERVER, 11100))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        try:
            return socket.gethostbyname(socket.gethostname())
        except Exception:
            return ""


def can_reach_campus_network() -> bool:
    """Checks if campus LAN/server or campus Tailscale endpoint is reachable."""
    targets = [
        (CLASSROOM_SERVER, 11100),
        (CLASSROOM_SERVER, 445),
        (TAILSCALE_SERVER, 11100),
        ("computer-50.local", 11100),
    ]
    for host, port in targets:
        s = None
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.6)
            s.connect((host, port))
            return True
        except Exception:
            pass
        finally:
            if s is not None:
                try:
                    s.close()
                except Exception:
                    pass
    return False


def get_service_state(service_name: str) -> str:
    """Returns 'RUNNING', 'STOPPED', 'PENDING', or 'MISSING'."""
    try:
        proc = subprocess.run(
            ["sc", "query", service_name],
            capture_output=True, text=True, timeout=3, creationflags=0x08000000,
        )
        out = proc.stdout.upper()
        if proc.returncode != 0 and "1060" in (proc.stdout + proc.stderr):
            return "MISSING"
        if "RUNNING" in out:
            return "RUNNING"
        if "STOPPED" in out:
            return "STOPPED"
        if "PENDING" in out:
            return "PENDING"
    except Exception:
        pass
    return "UNKNOWN"


def set_service_running(service_name: str, should_run: bool) -> None:
    state = get_service_state(service_name)
    if state == "MISSING":
        return
    try:
        if should_run and state == "STOPPED":
            log(f"Starting {service_name} (Campus Mode)")
            subprocess.run(["sc", "start", service_name], capture_output=True, timeout=5, creationflags=0x08000000)
        elif not should_run and state == "RUNNING":
            log(f"Stopping {service_name} (Personal/Paused/Revoked Mode)")
            subprocess.run(["sc", "stop", service_name], capture_output=True, timeout=5, creationflags=0x08000000)
    except Exception as e:
        log(f"set_service_running({service_name}, {should_run}) error: {e}")


def compute_effective_mode(campus_reachable: bool, policy: dict) -> tuple[str, str]:
    """Determines effective BYOD mode and reason without locking the user's laptop."""
    if policy.get("revoked"):
        return MODE_REVOKED, "enrollment_revoked"
    if policy.get("paused"):
        return MODE_PAUSED, "temporarily_paused"
    if policy.get("released"):
        return MODE_PERSONAL, "teacher_released"

    forced = policy.get("forced_mode")
    if forced == MODE_PERSONAL:
        return MODE_PERSONAL, "admin_override_personal"
    if forced == MODE_CAMPUS:
        return MODE_CAMPUS, "admin_override_campus"

    if campus_reachable:
        return MODE_CAMPUS, "campus_network_detected"
    return MODE_PERSONAL, "off_campus_or_disconnected"


def write_mode_state(state_payload: dict) -> None:
    try:
        os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
        tmp = STATE_FILE + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(state_payload, f, indent=2)
        os.replace(tmp, STATE_FILE)
    except Exception as e:
        log(f"write_mode_state error: {e}")


def send_telemetry_beacon(mode: str, reason: str, ts_identity: dict) -> None:
    """Sends non-blocking UDP discovery & mode telemetry to SynapseFleetDiscovery."""
    payload = {
        "hostname": socket.gethostname().upper(),
        "ip": get_local_lan_ip(),
        "mac": get_primary_mac(),
        "tailscale_node_id": ts_identity.get("tailscale_node_id", ""),
        "tailscale_ip": ts_identity.get("tailscale_ip", ""),
        "mode": mode,
        "reason": reason,
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    encoded = json.dumps(payload).encode("utf-8")
    for target in (CLASSROOM_SERVER, TAILSCALE_SERVER):
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(0.4)
            sock.sendto(encoded, (target, BEACON_UDP_PORT))
        except Exception:
            pass
        finally:
            if sock is not None:
                try:
                    sock.close()
                except Exception:
                    pass


def show_status_window() -> None:
    """Optional non-blocking status & admin-override window (--ui flag)."""
    import tkinter as tk
    from tkinter import simpledialog, messagebox

    root = tk.Tk()
    root.title("Synapse BYOD Status")
    root.geometry("420x250")
    root.configure(bg="#101114")
    root.resizable(False, False)

    lbl_mode = tk.Label(root, text="Checking mode...", font=("Segoe UI", 14, "bold"), bg="#101114", fg="#B7E34B")
    lbl_mode.pack(pady=(20, 6))

    lbl_detail = tk.Label(root, text="", font=("Segoe UI", 10), bg="#101114", fg="#252830", wraplength=380)
    lbl_detail.pack(pady=(0, 14))

    def refresh_view():
        if os.path.exists(STATE_FILE):
            try:
                with open(STATE_FILE, "r", encoding="utf-8") as f:
                    st = json.load(f)
                m = st.get("mode", "personal").upper()
                color = "#35C982" if m == "CAMPUS" else "#B7E34B"
                lbl_mode.config(text=f"Mode: {m}", fg=color)
                lbl_detail.config(
                    text=(
                        f"Reason: {st.get('reason', '-')}\n"
                        f"Tailscale Node ID: {st.get('tailscale_node_id') or 'Not connected'}\n"
                        f"Updated: {st.get('updated_at_utc', '-')}"
                    )
                )
            except Exception:
                pass
        root.after(2000, refresh_view)

    def admin_override():
        pwd = simpledialog.askstring("Admin Override", "Enter Teacher/Admin Password:", show="*", parent=root)
        if pwd is None:
            return
        if not verify_master_password(pwd):
            messagebox.showerror("Denied", "Incorrect administrator password.", parent=root)
            return
        pol = load_policy()
        currently_paused = bool(pol.get("paused") or pol.get("released"))
        pol["paused"] = not currently_paused
        pol["released"] = False
        pol["forced_mode"] = None
        save_policy(pol)
        messagebox.showinfo("Updated", f"BYOD supervision {'PAUSED (Personal Mode)' if pol['paused'] else 'RESUMED (Auto Campus Mode)'}.", parent=root)

    tk.Button(
        root, text="Admin Pause / Resume Override", command=admin_override,
        bg="#B7E34B", fg="#191B20", font=("Segoe UI", 10, "bold"), relief=tk.FLAT, padx=14, pady=6, cursor="hand2"
    ).pack(pady=8)

    refresh_view()
    root.mainloop()


def run_controller_loop() -> None:
    # Single-Instance Enforcement
    kernel32 = ctypes.windll.kernel32
    _mutex_handle = kernel32.CreateMutexW(None, False, "Global\\SynapseBYODGuard_SingleInstance_Mutex")
    if kernel32.GetLastError() == 183:
        sys.exit(0)

    log("==========================================")
    log("SynapseBYODGuard Non-Blocking Campus/Personal Controller starting...")
    clear_legacy_lock_policies()

    last_mode = None
    last_beacon = 0.0

    while True:
        try:
            policy = load_policy()
            campus_reachable = can_reach_campus_network()
            mode, reason = compute_effective_mode(campus_reachable, policy)

            # Keep Tailscale available in Campus Mode; leave user's PC unrestricted in Personal Mode
            if mode == MODE_CAMPUS:
                set_service_running("Tailscale", True)
                set_service_running(DIGIAGENT_SERVICE, True)
            else:
                # Personal / Paused / Revoked -> stop campus supervision, never lock laptop
                set_service_running(DIGIAGENT_SERVICE, False)

            ts_identity = get_tailscale_identity()
            now = time.time()

            if mode != last_mode:
                log(f"Mode transition: {last_mode} -> {mode} ({reason})")
                last_mode = mode

            write_mode_state({
                "mode": mode,
                "reason": reason,
                "campus_reachable": campus_reachable,
                "tailscale_node_id": ts_identity.get("tailscale_node_id", ""),
                "tailscale_ip": ts_identity.get("tailscale_ip", ""),
                "tailscale_dns": ts_identity.get("tailscale_dns", ""),
                "policy": policy,
                "updated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            })

            if now - last_beacon >= 15.0:
                send_telemetry_beacon(mode, reason, ts_identity)
                last_beacon = now

            time.sleep(3.0)
        except Exception as e:
            log(f"Controller loop exception: {e}")
            time.sleep(3.0)


def main() -> None:
    if "--ui" in sys.argv:
        show_status_window()
    elif "--status" in sys.argv:
        if os.path.exists(STATE_FILE):
            with open(STATE_FILE, "r", encoding="utf-8") as f:
                print(f.read())
        else:
            print(json.dumps({"mode": "unknown"}))
    else:
        run_controller_loop()


if __name__ == "__main__":
    main()
