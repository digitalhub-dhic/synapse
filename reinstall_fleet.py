"""
Synapse Fleet Parallel Reinstaller & Timer Fix Deployer v4.11.8.1
Cleans corrupted lock states, reinstalls patched core binaries in parallel.
"""

import os
import sys
import time
import socket
import subprocess
import shutil
import concurrent.futures

LAB_COMPUTERS = [
    ("192.168.1.101", "34:5A:60:48:3D:F9"),
    ("192.168.1.102", "34:5A:60:48:3D:F4"),
    ("192.168.1.103", ""),
    ("192.168.1.104", ""),
    ("192.168.1.105", "D8:43:AE:3D:CE:9B"),
    ("192.168.1.106", ""),
    ("192.168.1.107", "34:5A:60:48:3D:F7"),
    ("192.168.1.108", ""),
    ("192.168.1.109", "34:5A:60:48:3D:FA"),
    ("192.168.1.110", "34:5A:60:48:3D:E4"),
    ("192.168.1.111", ""),
    ("192.168.1.112", "D8:5E:D3:9A:F6:2A"),
    ("192.168.1.113", "34:5A:60:48:3D:84"),
    ("192.168.1.114", ""),
    ("192.168.1.115", "A8:A1:59:8A:82:07"),
    ("192.168.1.116", "A8:A1:59:92:CD:21"),
    ("192.168.1.117", "A8:A1:59:8A:D2:9E"),
    ("192.168.1.118", ""),
    ("192.168.1.119", ""),
    ("192.168.1.120", ""),
    ("192.168.1.121", ""),
    ("192.168.1.122", ""),
    ("192.168.1.123", "A8:A1:59:88:CB:E9"),
    ("192.168.1.124", "A8:A1:59:8A:D2:FD"),
    ("192.168.1.125", "D8:5E:D3:9A:F6:36"),
    ("192.168.1.126", ""),
    ("192.168.1.127", "D8:5E:D3:9A:F6:2C"),
    ("192.168.1.128", "A8:A1:59:8A:D2:D3"),
    ("192.168.1.129", "EC:8E:B5:66:C6:A9"),
    ("192.168.1.130", ""),
    ("192.168.1.131", ""),
    ("192.168.1.132", "E4:54:E8:9D:1E:59"),
    ("192.168.1.133", ""),
    ("192.168.1.138", ""),
    ("192.168.1.144", "34:5A:60:48:3D:F8"),
    ("192.168.1.145", ""),
    ("192.168.1.146", ""),
    ("192.168.1.147", "D8:5E:D3:9A:F5:E3"),
    ("192.168.1.148", ""),
    ("192.168.1.150", "E4:54:E8:9D:25:17"),
    ("192.168.1.22",  "D8:5E:D3:9A:F6:93"),
    ("192.168.1.249", "BC:FC:E7:4E:00:53"),
    ("192.168.1.200", "30:C5:99:7C:BA:4D"),
]

SRC_CORE = r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\DigiAgentCore.dll"
SRC_PLUGINS = r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\plugins"
SRC_CONFIG = r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\client_config.json"
SRC_SCRIPT = r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1"

def send_wol(mac):
    if not mac:
        return
    try:
        raw_mac = bytes.fromhex(mac.replace(":", "").replace("-", ""))
        packet = b"\xff" * 6 + raw_mac * 16
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            s.sendto(packet, ("192.168.1.255", 9))
            s.sendto(packet, ("255.255.255.255", 9))
    except Exception:
        pass

def check_tcp(ip, port=445, timeout=0.5):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        res = s.connect_ex((ip, port))
        s.close()
        return res == 0
    except Exception:
        return False

def reinstall_computer(item):
    ip, mac = item
    if not check_tcp(ip, 445, 0.4):
        return ip, False, "Offline (SMB port closed)"

    c_prog = rf"\\{ip}\c$\Program Files\DigiAgent"
    c_pdata = rf"\\{ip}\c$\ProgramData\DigiAgent"
    
    try:
        # 1. Kill any hung or active processes
        for proc in ["digiagent-server.exe", "digiagent-worker.exe", "SynapseWatchdog.exe", "SynapseBYODGuard.exe"]:
            subprocess.run(f"taskkill.exe /s {ip} /im {proc} /f", shell=True, capture_output=True)
        subprocess.run(f"sc.exe \\\\{ip} stop DigiAgentService", shell=True, capture_output=True)
        time.sleep(1)
        
        # 2. Clear out any stuck persistent lock files
        for bad_file in ["persistent_lock.json", "kiosk_locked.state"]:
            bf_path = os.path.join(c_pdata, bad_file)
            if os.path.exists(bf_path):
                try:
                    os.remove(bf_path)
                    print(f"  [-] Removed stuck lock file on {ip}: {bad_file}")
                except Exception:
                    pass

        # 3. Reinstall binaries cleanly
        os.makedirs(c_prog, exist_ok=True)
        shutil.copy2(SRC_CORE, os.path.join(c_prog, "DigiAgentCore.dll"))
        
        dst_plugins = os.path.join(c_prog, "plugins")
        os.makedirs(dst_plugins, exist_ok=True)
        for pfile in os.listdir(SRC_PLUGINS):
            if pfile.lower().endswith(".dll"):
                shutil.copy2(os.path.join(SRC_PLUGINS, pfile), os.path.join(dst_plugins, pfile))
                
        # 4. Reinstall configs & updater script
        os.makedirs(c_pdata, exist_ok=True)
        shutil.copy2(SRC_CONFIG, os.path.join(c_pdata, "client_config.json"))
        shutil.copy2(SRC_SCRIPT, os.path.join(c_pdata, "Check-And-AutoUpdate.ps1"))
        
        # 5. Set version to 4.11.8.8
        with open(os.path.join(c_pdata, "installed_version.txt"), "w", encoding="utf-8") as f:
            f.write("4.11.8.8")
            
        # 6. Ensure auto-start and service start
        subprocess.run(f"sc.exe \\\\{ip} config DigiAgentService start= auto", shell=True, capture_output=True)
        subprocess.run(f"sc.exe \\\\{ip} failure DigiAgentService reset= 0 actions= restart/1000/restart/2000/restart/5000", shell=True, capture_output=True)
        subprocess.run(f"sc.exe \\\\{ip} start DigiAgentService", shell=True, capture_output=True)
        
        print(f"  [OK] Successfully reinstalled & restarted on {ip} (v4.11.8.8)")
        return ip, True, "Reinstalled v4.11.8.8"
    except Exception as e:
        subprocess.run(f"sc.exe \\\\{ip} start DigiAgentService", shell=True, capture_output=True)
        print(f"  [ERR] {ip}: {e}")
        return ip, False, str(e)

def main():
    print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] Broadcasting Wake-on-LAN packets...")
    for _, mac in LAB_COMPUTERS:
        send_wol(mac)
    time.sleep(1)

    print("[*] Reinstalling fleet in parallel...")
    with concurrent.futures.ThreadPoolExecutor(max_workers=30) as executor:
        results = list(executor.map(reinstall_computer, LAB_COMPUTERS))

    print("\n" + "=" * 62)
    print("REINSTALLATION SUMMARY:")
    print("=" * 62)
    for ip, succ, msg in sorted(results, key=lambda x: [int(p) for p in x[0].split('.')]):
        status_str = "SUCCESS" if succ else "OFFLINE"
        print(f"{ip:<16} | {status_str:<10} | {msg}")

if __name__ == "__main__":
    main()
