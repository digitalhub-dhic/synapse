"""
Synapse Dynamic Fleet Auto-Discovery & Zero-Config Registration Engine v4.11.7.0 (Anti-Duplication Edition)
Automatically discovers and enrolls ANY new computer that installs Synapse into the teacher's monitoring panel.
Guarantees NO duplicates for lab computers or existing entries.
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
    """Returns { 'by_name': { name.lower(): [ { ... } ] }, 'by_ip': { ip: [ { ... } ] } }"""
    if not os.path.exists(CLI_PATH):
        return {"by_name": {}, "by_ip": {}}
    try:
        res = subprocess.run([CLI_PATH, "networkobjects", "list"], capture_output=True, text=True, timeout=10)
        lines = res.stdout.splitlines()
        current_loc = "DIGIHUB"
        by_name = {}
        by_ip = {}
        for line in lines:
            line_str = line.strip()
            if line_str.startswith('Location "'):
                m = re.match(r'Location\s+"([^"]+)"', line_str)
                if m:
                    current_loc = m.group(1)
            elif line_str.startswith('Computer "'):
                m = re.match(r'Computer\s+"([^"]+)"\s+\(host address:\s+"([^"]*)"\s+MAC address:\s+"([^"]*)"\)', line_str)
                if m:
                    name = m.group(1)
                    host = m.group(2)
                    mac = m.group(3)
                    entry = {
                        "name": name,
                        "host": host,
                        "mac": mac,
                        "location": current_loc
                    }
                    k_name = name.lower()
                    if k_name not in by_name:
                        by_name[k_name] = []
                    by_name[k_name].append(entry)

                    if host:
                        if host not in by_ip:
                            by_ip[host] = []
                        by_ip[host].append(entry)

        return {"by_name": by_name, "by_ip": by_ip}
    except Exception as e:
        print(f"[ERR] Error listing network objects: {e}")
        return {"by_name": {}, "by_ip": {}}

def enroll_computer(name, host, mac="", location="BYOD"):
    """Enrolls a computer into a specific location in DigiAgent networkobjects."""
    if not os.path.exists(CLI_PATH) or not name or not host:
        return False

    name_clean = name.strip()
    host_clean = host.strip()
    mac_clean = mac.strip()

    cmd = [CLI_PATH, "networkobjects", "add", "computer", name_clean, host_clean, mac_clean, location]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=8)
        if "[OK]" in res.stdout:
            print(f"[AUTO-ENROLL] Successfully registered new device '{name_clean}' ({host_clean}) into '{location}'!")
            return True
        return False
    except Exception as e:
        print(f"[AUTO-ENROLL ERR] {e}")
        return False

def sync_device(name, current_ip, mac=""):
    """Registers ONLY genuinely new devices, strictly avoiding duplicates."""
    registered = get_registered_network_objects()
    by_name = registered["by_name"]
    by_ip = registered["by_ip"]

    # 1. If this IP is ALREADY mapped to any computer, DO NOT create a duplicate entry!
    if current_ip in by_ip:
        existing_entries = by_ip[current_ip]
        existing_names = [e["name"] for e in existing_entries]
        # Already registered cleanly (e.g. 'computer 02' or 'READINGHUB')
        return False

    # 2. If name is already registered, check if its IP needs an update
    key = name.lower()
    if key in by_name:
        entries = by_name[key]
        for entry in entries:
            old_host = entry["host"]
            if old_host != current_ip and not old_host.isalpha():
                print(f"[IP-UPDATE] Computer '{name}' IP moved from {old_host} -> {current_ip}. Updating...")
                subprocess.run([CLI_PATH, "networkobjects", "remove", name], capture_output=True, timeout=8)
                time.sleep(0.3)
                enroll_computer(name, current_ip, mac or entry.get("mac", ""), "BYOD")
                return True
        return False

    # 3. Genuinely new unknown device (e.g. new student laptop)
    print(f"[NEW-DEVICE] Enrolling previously unseen device '{name}' @ {current_ip} into BYOD...")
    enroll_computer(name, current_ip, mac, "BYOD")
    return True

def probe_agent_port(ip, timeout=0.6):
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
