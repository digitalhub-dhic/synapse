import subprocess
import json
import base64
import time

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

$out | Out-File -FilePath "\\\\192.168.1.50\\public\\Synapse\\laptop_report.json" -Encoding utf8 -Force
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
print("CLI stderr:", res.stderr)
