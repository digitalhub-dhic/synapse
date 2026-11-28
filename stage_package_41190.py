import os
import sys
import shutil
import subprocess
import re

codex_dir = r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4"
base_build = os.path.join(codex_dir, r"work\DigiAgent-build-output")
src_pkg = os.path.join(base_build, "digiagent-win64-4.11.4.9")
dst_pkg = os.path.join(base_build, "digiagent-win64-4.11.9.0")

def handle_remove_readonly(func, path, exc):
    import stat
    os.chmod(path, stat.S_IWRITE)
    func(path)

print("[1] Staging directory:", dst_pkg)
if os.path.exists(dst_pkg):
    shutil.rmtree(dst_pkg, onexc=handle_remove_readonly)

# Copy base package
shutil.copytree(src_pkg, dst_pkg)

# Remove old setup executables from staging
for f in os.listdir(dst_pkg):
    if (f.startswith("Synapse-") or f.startswith("DigiAgent-")) and f.endswith(".exe"):
        os.remove(os.path.join(dst_pkg, f))

# Copy freshly built binaries
binaries = [
    (r"core\DigiAgentCore.dll", "DigiAgentCore.dll"),
    (r"master\veyon-master.exe", "digiagent-master.exe"),
    (r"configurator\veyon-configurator.exe", "DigiClient.exe"),
    (r"configurator\veyon-configurator.exe", "digiagent-configurator.exe"),
    (r"service\veyon-service.exe", "digiagent-service.exe"),
    (r"server\veyon-server.exe", "digiagent-server.exe"),
    (r"worker\veyon-worker.exe", "digiagent-worker.exe"),
    (r"cli\veyon-cli.exe", "digiagent-cli.exe"),
    (r"cli\veyon-wcli.exe", "digiagent-wcli.exe"),
]

for src_rel, dst_name in binaries:
    s = os.path.join(base_build, src_rel)
    d = os.path.join(dst_pkg, dst_name)
    shutil.copy2(s, d)
    print(f"Copied {src_rel} -> {dst_name} ({os.path.getsize(d)} bytes)")

# Copy plugins
dst_plugins = os.path.join(dst_pkg, "plugins")
os.makedirs(dst_plugins, exist_ok=True)
src_plugins = os.path.join(base_build, "plugins")
for root, dirs, files in os.walk(src_plugins):
    if "CMakeFiles" in root:
        continue
    for f in files:
        if f.endswith(".dll"):
            s = os.path.join(root, f)
            d = os.path.join(dst_plugins, f)
            shutil.copy2(s, d)
            print(f"Copied plugin {f} ({os.path.getsize(d)} bytes)")

# Update veyon.nsi version
nsi_path = os.path.join(dst_pkg, "veyon.nsi")
with open(nsi_path, "r", encoding="utf-8", errors="ignore") as f:
    nsi_content = f.read()

nsi_content = re.sub(r'!define VERSION "[0-9\.]+"', '!define VERSION "4.11.9.0"', nsi_content)

with open(nsi_path, "w", encoding="utf-8") as f:
    f.write(nsi_content)

print("[OK] Staging complete. Updated veyon.nsi to version 4.11.9.0")
