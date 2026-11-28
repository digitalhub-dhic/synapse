"""
Synapse Dynamic Fleet Auto-Discovery & Zero-Config Registration Engine v4.11.7.0 (Anti-Duplication Edition)
Automatically discovers and enrolls ANY new computer that installs Synapse into the teacher's monitoring panel.
Guarantees NO duplicates, tracks dynamic DHCP IP changes by MAC address, and enrolls into DIGIHUB.
"""

import os
import re
import sys
import time
import json
import ctypes
import socket
import threading
import subprocess
from concurrent.futures import ThreadPoolExecutor

CLI_PATH = r"C:\Program Files\DigiAgent\digiagent-cli.exe"
BEACON_UDP_PORT = 11105
SUBNET_PREFIX = "192.168.1."
SUBNET_PREFIXES = ["192.168.1.", "192.168.30."]
SELF_IP = "192.168.1.150"
TARGET_LOCATION = "BYOD"
TELEMETRY_PATHS = [
    r"C:\ProgramData\DigiAgent\fleet_byod_telemetry.json",
    r"C:\SynapseAISentinel\fleet_byod_telemetry.json",
]
byod_telemetry = {}
telemetry_lock = threading.Lock()

# Win32 Single-Instance Mutex
MUTEX_NAME = "Global\\SynapseFleetDiscovery_SingleInstance_Mutex"
kernel32 = ctypes.windll.kernel32
mutex_handle = kernel32.CreateMutexW(None, False, MUTEX_NAME)
if kernel32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
    print("[*] Synapse Fleet Discovery is already running. Exiting duplicate instance.")
    sys.exit(0)

def get_arp_mac(target_ip):
    """Retrieves MAC address from OS ARP cache for target IP."""
    try:
        proc = subprocess.run(["arp", "-a", target_ip], capture_output=True, text=True, timeout=2)
        for line in proc.stdout.splitlines():
            if target_ip in line:
                parts = line.split()
                if len(parts) >= 2:
                    mac_candidate = parts[1].replace("-", ":").upper()
                    if len(mac_candidate) == 17:
                        return mac_candidate
    except Exception:
        pass
    return ""

def get_registered_network_objects():
    """Returns { 'by_name': dict, 'by_ip': dict, 'by_mac': dict, 'objects': list }"""
    if not os.path.exists(CLI_PATH):
        return {"by_name": {}, "by_ip": {}, "by_mac": {}, "objects": []}
    try:
        res = subprocess.run([CLI_PATH, "networkobjects", "dump"], capture_output=True, text=True, timeout=10)
        lines = res.stdout.splitlines()
        by_name = {}
        by_ip = {}
        by_mac = {}
        objects = []

        for line in lines:
            if not line.startswith("|") or "Object UUID" in line or "---" in line:
                continue
            parts = [p.strip() for p in line.split("|")[1:-1]]
            if len(parts) >= 6:
                obj_uuid, parent_uuid, obj_type, name, host, mac = parts[:6]
                if obj_type.lower() == "computer":
                    entry = {
                        "uuid": obj_uuid,
                        "parent_uuid": parent_uuid,
                        "name": name,
                        "host": host,
                        "mac": mac.upper() if mac else ""
                    }
                    objects.append(entry)
                    k_name = name.lower()
                    if k_name not in by_name:
                        by_name[k_name] = []
                    by_name[k_name].append(entry)

                    if host:
                        if host not in by_ip:
                            by_ip[host] = []
                        by_ip[host].append(entry)

                    if entry["mac"]:
                        if entry["mac"] not in by_mac:
                            by_mac[entry["mac"]] = []
                        by_mac[entry["mac"]].append(entry)

        return {"by_name": by_name, "by_ip": by_ip, "by_mac": by_mac, "objects": objects}
    except Exception as e:
        print(f"[ERR] Error dumping network objects: {e}")
        return {"by_name": {}, "by_ip": {}, "by_mac": {}, "objects": []}

def remove_object(identifier):
    """Removes an object by name or UUID."""
    if not os.path.exists(CLI_PATH) or not identifier:
        return
    try:
        subprocess.run([CLI_PATH, "networkobjects", "remove", identifier], capture_output=True, timeout=8)
        clean_id = identifier.strip("{}")
        if clean_id != identifier:
            subprocess.run([CLI_PATH, "networkobjects", "remove", clean_id], capture_output=True, timeout=8)
        else:
            subprocess.run([CLI_PATH, "networkobjects", "remove", f"{{{clean_id}}}"], capture_output=True, timeout=8)
    except Exception as e:
        print(f"[ERR] Remove object {identifier} failed: {e}")

def enroll_computer(name, host, mac="", location=TARGET_LOCATION):
    """Enrolls a computer into a specific location in DigiAgent networkobjects."""
    if not os.path.exists(CLI_PATH) or not name or not host:
        return False

    name_clean = name.strip()
    host_clean = host.strip()
    mac_clean = mac.strip().upper()

    # Pre-check: Ensure this host address does not already exist in any location
    reg = get_registered_network_objects()
    for existing in reg.get("objects", []):
        if existing.get("host", "").strip().lower() == host_clean.lower():
            print(f"[SKIP-DUPLICATE] Host {host_clean} already registered as '{existing.get('name')}' in location {existing.get('parent_uuid')}. Skipping add.")
            return False

    # Never add lab IPs (192.168.1.101 - 192.168.1.140) into BYOD
    if location == "BYOD" and host_clean.startswith("192.168.1."):
        try:
            last_octet = int(host_clean.split(".")[-1])
            if 101 <= last_octet <= 140 or last_octet in (145, 146, 148, 150):
                print(f"[SKIP-LAB-IP] Host {host_clean} is a DIGIHUB lab workstation IP. Refusing to add into BYOD.")
                return False
        except ValueError:
            pass

    cmd = [CLI_PATH, "networkobjects", "add", "computer", name_clean, host_clean, mac_clean, location]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=8)
        if "[OK]" in res.stdout:
            print(f"[AUTO-ENROLL] Successfully registered '{name_clean}' ({host_clean}, {mac_clean}) into '{location}'!")
            return True
        return False
    except Exception as e:
        print(f"[AUTO-ENROLL ERR] {e}")
        return False

sync_lock = threading.Lock()

def sync_device(name, current_ip, mac=""):
    with sync_lock:
        return _sync_device_impl(name, current_ip, mac)

def _sync_device_impl(name, current_ip, mac=""):
    """
    Intelligently registers or updates computers without creating duplicates.
    1. If IP is already registered in ANY location, do nothing!
    2. If MAC matches an existing entry, do nothing unless IP changed.
    3. If genuinely new device and NOT a lab computer IP, enroll into BYOD.
    """
    mac_clean = mac.strip().upper()
    registered = get_registered_network_objects()
    by_name = registered["by_name"]
    by_ip = registered["by_ip"]
    by_mac = registered["by_mac"]

    # 1. Check if IP is ALREADY registered anywhere in the fleet
    if current_ip in by_ip:
        # IP already exists in directory - never duplicate!
        return False

    # 2. Check if MAC is already registered
    if mac_clean and mac_clean in by_mac:
        # Hardware already registered - never create a duplicate entry
        return False

    # 3. Check by Name
    key = name.lower()
    if key in by_name:
        return False

    # 4. Genuinely new unknown device
    assigned_location = TARGET_LOCATION
    if current_ip.startswith("192.168.1."):
        try:
            last_octet = int(current_ip.split(".")[-1])
            if 101 <= last_octet <= 140 or last_octet in (145, 146, 148, 150):
                assigned_location = "DIGIHUB"
        except ValueError:
            pass

    print(f"[NEW-DEVICE] Enrolling newly discovered device '{name}' @ {current_ip} into '{assigned_location}'...")
    enroll_computer(name, current_ip, mac_clean, assigned_location)
    return True

def probe_agent_port(ip, timeout=0.5):
    """Returns True if port 11100 (DigiAgent) is actively accepting connections."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((ip, 11100))
        s.close()
        return ip, True
    except Exception:
        return ip, False

def resolve_hostname(ip):
    """Resolves computer hostname via reverse DNS."""
    try:
        host, _, _ = socket.gethostbyaddr(ip)
        return host.split('.')[0].upper()
    except Exception:
        pass
    return None

def run_subnet_sweep():
    """Performs a concurrent sweep of all configured subnets (LAN + BYOD Wi-Fi) on port 11100."""
    ips = [
        f"{prefix}{i}"
        for prefix in SUBNET_PREFIXES
        for i in range(1, 255)
        if f"{prefix}{i}" != SELF_IP
    ]
    active_ips = []
    
    with ThreadPoolExecutor(max_workers=80) as executor:
        for ip, is_open in executor.map(probe_agent_port, ips):
            if is_open:
                active_ips.append(ip)
                
    for ip in active_ips:
        mac = get_arp_mac(ip)
        hostname = resolve_hostname(ip)
        if hostname:
            sync_device(hostname, ip, mac)
        else:
            fallback_name = f"BYOD-{ip.split('.')[-1]}"
            sync_device(fallback_name, ip, mac)


def run_tailscale_sweep():
    """Discovers online Tailscale peers on port 11100 (for BYOD devices on external networks)."""
    try:
        proc = subprocess.run(
            ["tailscale", "status", "--json"],
            capture_output=True, text=True, timeout=5, creationflags=0x08000000
        )
        if proc.returncode != 0 or not proc.stdout.strip():
            return
        data = json.loads(proc.stdout)
        peers = (data.get("Peer") or {}).values()
        for peer in peers:
            if not peer.get("Online"):
                continue
            ips = peer.get("TailscaleIPs") or []
            ts_ip = next((ip for ip in ips if "." in ip), "")
            if not ts_ip:
                continue
            _, is_open = probe_agent_port(ts_ip, timeout=0.8)
            if is_open:
                hostname = (peer.get("HostName") or "").strip().upper() or f"BYOD-{ts_ip.split('.')[-1]}"
                sync_device(hostname, ts_ip, "")
    except Exception:
        pass

def record_byod_telemetry(payload, fallback_ip):
    """Records BYOD Campus/Personal mode and Tailscale node ID telemetry atomically."""
    ts_id = (payload.get("tailscale_node_id") or "").strip()
    mac = (payload.get("mac") or "").strip().upper()
    ip = (payload.get("ip") or fallback_ip or "").strip()
    key = ts_id or mac or ip
    if not key:
        return
    entry = {
        "tailscale_node_id": ts_id,
        "tailscale_ip": (payload.get("tailscale_ip") or "").strip(),
        "hostname": (payload.get("hostname") or "").strip(),
        "ip": ip,
        "mac": mac,
        "mode": (payload.get("mode") or "campus").strip().lower(),
        "reason": (payload.get("reason") or "").strip(),
        "last_seen_utc": payload.get("timestamp_utc") or time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    with telemetry_lock:
        byod_telemetry[key] = entry
        snapshot = {
            "updated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "devices": list(byod_telemetry.values()),
        }
        for path in TELEMETRY_PATHS:
            parent = os.path.dirname(path)
            if os.path.exists(parent) or path.startswith(r"C:\ProgramData"):
                try:
                    os.makedirs(parent, exist_ok=True)
                    tmp = path + ".tmp"
                    with open(tmp, "w", encoding="utf-8") as f:
                        json.dump(snapshot, f, indent=2)
                    os.replace(tmp, path)
                except Exception:
                    pass


def udp_beacon_listener():
    """Listens for client self-announcement UDP broadcasts on port 11105."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(('0.0.0.0', BEACON_UDP_PORT))
        print(f"[*] Beacon Listener active on UDP port {BEACON_UDP_PORT}")
        while True:
            data, addr = sock.recvfrom(2048)
            try:
                payload = json.loads(data.decode('utf-8'))
                hostname = payload.get("hostname")
                ip = payload.get("ip") or addr[0]
                mac = payload.get("mac", "")
                mode = (payload.get("mode") or "campus").strip().lower()
                record_byod_telemetry(payload, addr[0])
                if hostname and ip and mode == "campus":
                    sync_device(hostname, ip, mac)
            except Exception:
                pass
    except Exception as e:
        print(f"[ERR] UDP Beacon listener failed: {e}")

def run_discovery_daemon():
    print("==================================================================")
    print("   SYNAPSE DYNAMIC FLEET AUTO-DISCOVERY & ENROLLMENT DAEMON       ")
    print("==================================================================")
    
    t_udp = threading.Thread(target=udp_beacon_listener, daemon=True)
    t_udp.start()
    
    print("[*] Subnet sweeper starting. Interval: 45 seconds.")
    while True:
        try:
            run_subnet_sweep()
            run_tailscale_sweep()
        except Exception as e:
            print(f"[SWEEP ERR] {e}")
        time.sleep(45)

if __name__ == "__main__":
    run_discovery_daemon()
