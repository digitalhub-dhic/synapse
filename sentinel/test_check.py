import subprocess
import socket

def test_wifi():
    proc = subprocess.run(['netsh', 'wlan', 'show', 'interfaces'], capture_output=True, text=True, creationflags=0x08000000)
    for line in proc.stdout.splitlines():
        line = line.strip().lower()
        if line.startswith('state') and 'connected' in line:
            return True
    return False

def test_classroom():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1.5)
        s.connect(('192.168.1.150', 11100))
        s.close()
        return True
    except Exception:
        return False

print(f'WIFI_STATUS: {test_wifi()}')
print(f'CLASSROOM_REACHABLE: {test_classroom()}')
