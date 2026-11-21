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
SELF_IP = "192.168.1.150"
TARGET_LOCATION = "BYOD"

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
    except Exception as e:
        print(f"[ERR] Remove object {identifier} failed: {e}")

def enroll_computer(name, host, mac="", location=TARGET_LOCATION):
    """Enrolls a computer into a specific location in DigiAgent networkobjects."""
    if not os.path.exists(CLI_PATH) or not name or not host:
        return False

    name_clean = name.strip()
    host_clean = host.strip()
    mac_clean = mac.strip().upper()

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

def sync_device(name, current_ip, mac=""):
    """
    Intelligently registers or updates computers without creating duplicates.
    1. If MAC matches an existing entry, update IP if changed.
    2. If IP matches an existing entry with the same name, nothing to do.
    3. If IP matches an existing entry with a stale name, update to correct hostname.
    4. If completely new, register into TARGET_LOCATION (DIGIHUB).
    """
    mac_clean = mac.strip().upper()
    registered = get_registered_network_objects()
    by_name = registered["by_name"]
    by_ip = registered["by_ip"]
    by_mac = registered["by_mac"]

    # 1. Check by MAC Address first (strongest hardware identity across DHCP changes)
    if mac_clean and mac_clean in by_mac:
        entries = by_mac[mac_clean]
        for entry in entries:
            canonical_name = entry["name"]
            # If standard lab computer (e.g. "computer 01"), preserve its lab number!
            if canonical_name.lower().startswith("computer ") or canonical_name.lower().startswith("sub "):
                if entry["host"] != current_ip:
                    print(f"[LAB-IP-UPDATE] Lab station '{canonical_name}' ({mac_clean}) moved {entry['host']} -> {current_ip}")
                    remove_object(entry["uuid"])
                    time.sleep(0.2)
                    enroll_computer(canonical_name, current_ip, mac_clean, TARGET_LOCATION)
                return True
            else:
                # Named workstation (e.g. READINGHUB, CIVIL)
                preferred_name = name if name and not name.startswith("BYOD-") else canonical_name
                if entry["host"] != current_ip or entry["name"] != preferred_name:
                    print(f"[DEVICE-SYNC] Updating '{preferred_name}' ({mac_clean}) IP: {entry['host']} -> {current_ip}")
                    remove_object(entry["uuid"])
                    time.sleep(0.2)
                    enroll_computer(preferred_name, current_ip, mac_clean, TARGET_LOCATION)
                return True

    # 2. Check if IP is already registered
    if current_ip in by_ip:
        entries = by_ip[current_ip]
        for entry in entries:
            # If already cleanly registered under this name or as a lab computer
            if entry["name"].lower() == name.lower() or entry["name"].lower().startswith("computer "):
                return False
            # If registered under a generic/dummy name (e.g. DESKTOP-111111KOOOIP), replace with real hostname
            if name and not name.startswith("BYOD-"):
                print(f"[RENAME-STALE] Replacing stale entry '{entry['name']}' with real hostname '{name}' @ {current_ip}")
                remove_object(entry["uuid"])
                time.sleep(0.2)
                enroll_computer(name, current_ip, mac_clean, TARGET_LOCATION)
                return True
        return False

    # 3. Check by Name
    key = name.lower()
    if key in by_name:
        entries = by_name[key]
        for entry in entries:
            if entry["host"] != current_ip:
                print(f"[IP-UPDATE] Computer '{name}' IP moved from {entry['host']} -> {current_ip}. Updating...")
                remove_object(entry["uuid"])
                time.sleep(0.2)
                enroll_computer(name, current_ip, mac_clean or entry.get("mac", ""), TARGET_LOCATION)
                return True
        return False

    # 4. Genuinely new unknown device (new workstation / laptop)
    print(f"[NEW-DEVICE] Enrolling previously unseen device '{name}' @ {current_ip} into {TARGET_LOCATION}...")
    enroll_computer(name, current_ip, mac_clean, TARGET_LOCATION)
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
    """Performs a concurrent sweep of the subnet on port 11100."""
    ips = [f"{SUBNET_PREFIX}{i}" for i in range(1, 255) if f"{SUBNET_PREFIX}{i}" != SELF_IP]
    active_ips = []
    
    with ThreadPoolExecutor(max_workers=50) as executor:
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
                if hostname and ip:
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
        except Exception as e:
            print(f"[SWEEP ERR] {e}")
        time.sleep(45)

if __name__ == "__main__":
    run_discovery_daemon()
