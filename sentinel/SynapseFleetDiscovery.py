"""
Synapse Dynamic Fleet Auto-Discovery & Zero-Config Registration Engine v4.11.7.0
Automatically discovers and enrolls ANY computer that installs Synapse into the teacher's monitoring panel.
No manual IP registration or typing required.
"""

import os
import re
import sys
import time
import json
import ctypes
import socket
import select
import threading
import subprocess
from concurrent.futures import ThreadPoolExecutor

CLI_PATH = r"C:\Program Files\DigiAgent\digiagent-cli.exe"
BEACON_UDP_PORT = 11105
HTTP_PORT = 11085
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
    """Returns a dict of currently registered computers: { name.lower(): [ { 'name': ..., 'host': ..., 'mac': ..., 'location': ... } ] }"""
    if not os.path.exists(CLI_PATH):
        return {}
    try:
        res = subprocess.run([CLI_PATH, "networkobjects", "list"], capture_output=True, text=True, timeout=10)
        lines = res.stdout.splitlines()
        current_loc = "DIGIHUB"
        registered = {}
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
                    key = name.lower()
                    if key not in registered:
                        registered[key] = []
                    registered[key].append({
                        "name": name,
                        "host": host,
                        "mac": mac,
                        "location": current_loc
                    })
        return registered
    except Exception as e:
        print(f"[ERR] Error listing network objects: {e}")
        return {}

def enroll_computer(name, host, mac="", location="DIGIHUB"):
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
            print(f"[AUTO-ENROLL] Successfully registered '{name_clean}' ({host_clean}) into '{location}'!")
            return True
        elif "already exists" in res.stdout or "already exists" in res.stderr:
            return True
        else:
            return False
    except Exception as e:
        print(f"[AUTO-ENROLL ERR] {e}")
        return False

def sync_device(name, current_ip, mac=""):
    """Ensures device is enrolled in both DIGIHUB and BYOD, updating host IP if changed."""
    registered = get_registered_network_objects()
    key = name.lower()
    
    locations = ["DIGIHUB", "BYOD"]
    
    if key in registered:
        entries = registered[key]
        needs_re_enroll = False
        for entry in entries:
            old_host = entry["host"]
            # Check if IP has changed and old host is not a dynamic hostname
            if old_host != current_ip and old_host != name and old_host != f"{name}.local":
                print(f"[IP-UPDATE] Computer '{name}' IP changed ({old_host} -> {current_ip}). Re-indexing...")
                needs_re_enroll = True
                break
        
        if needs_re_enroll:
            subprocess.run([CLI_PATH, "networkobjects", "remove", name], capture_output=True, timeout=8)
            time.sleep(0.3)
            for loc in locations:
                enroll_computer(name, current_ip, mac or entries[0].get("mac", ""), loc)
            return True
            
        # Ensure it exists in both locations
        existing_locs = [e["location"] for e in entries]
        for loc in locations:
            if loc not in existing_locs:
                enroll_computer(name, current_ip, mac, loc)
        return False
    else:
        # Brand new detected device!
        print(f"[NEW-DISCOVERY] Found unlisted Synapse workstation: '{name}' @ {current_ip} [MAC: {mac}]")
        for loc in locations:
            enroll_computer(name, current_ip, mac, loc)
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
                
    # Process active IPs
    registered = get_registered_network_objects()
    known_hosts = set()
    for entries in registered.values():
        for e in entries:
            known_hosts.add(e["host"])
            
    for ip in active_ips:
        mac = get_arp_mac(ip)
        hostname = resolve_hostname(ip)
        if hostname:
            sync_device(hostname, ip, mac)
        elif ip not in known_hosts:
            # Fallback name if reverse DNS has no PTR record
            fallback_name = f"Client-{ip.split('.')[-1]}"
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
                    print(f"[BEACON] Received broadcast from '{hostname}' @ {ip}")
                    sync_device(hostname, ip, mac)
            except Exception:
                pass
    except Exception as e:
        print(f"[ERR] UDP Beacon listener failed: {e}")

def run_discovery_daemon():
    print("==================================================================")
    print("   SYNAPSE DYNAMIC FLEET AUTO-DISCOVERY & ENROLLMENT DAEMON       ")
    print("==================================================================")
    
    # 1. Start UDP beacon listener thread
    t_udp = threading.Thread(target=udp_beacon_listener, daemon=True)
    t_udp.start()
    
    # 2. Main loop: sweep subnet every 30 seconds
    print("[*] Subnet sweeper starting. Interval: 30 seconds.")
    while True:
        try:
            run_subnet_sweep()
        except Exception as e:
            print(f"[SWEEP ERR] {e}")
        time.sleep(30)

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--once":
        print("[*] Running single sweep and auto-enrollment...")
        run_subnet_sweep()
        print("[OK] Single sweep completed.")
    else:
        run_discovery_daemon()
