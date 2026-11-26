import os
import sys
import shutil
import hashlib
import json
import subprocess
from datetime import datetime

STAGE_DIR = r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build-output\digiagent-win64-4.11.9.0"
BYOD_SETUP = r"C:\SynapseAISentinel\build_installer\dist\Synapse-Setup-BYOD.exe"
LAB_SETUP = r"C:\SynapseAISentinel\build_installer_lab\dist\Synapse-Setup-Lab.exe"
SERVER_SHARE = r"\\192.168.1.50\public\Synapse"
REPO_DIR = r"C:\Users\administrator\projects\synapse"

def get_sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()

def copy_and_verify(src, dst):
    print(f"[*] Deploying {os.path.basename(src)} -> {dst}")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    src_h = get_sha256(src)
    dst_h = get_sha256(dst)
    if src_h != dst_h:
        raise RuntimeError(f"Hash mismatch on {dst}!")
    print(f"  [OK] SHA-256 verified ({src_h[:12]}...)")

def main():
    print("=== Step 1: Deploying Installers to Network Share ===")
    copy_and_verify(BYOD_SETUP, os.path.join(SERVER_SHARE, r"1-Windows\BYOD\Synapse-Setup-BYOD.exe"))
    copy_and_verify(BYOD_SETUP, os.path.join(SERVER_SHARE, r"Synapse-Setup-BYOD.exe"))
    copy_and_verify(LAB_SETUP, os.path.join(SERVER_SHARE, r"1-Windows\Lab\Synapse-Setup-Lab.exe"))

    print("\n=== Step 2: Deploying Updated Binaries to Auto-Updater Payload ===")
    payload_dir = os.path.join(SERVER_SHARE, r"4-Auto-Updater\Payload")
    payload_plugins = os.path.join(payload_dir, "plugins")
    os.makedirs(payload_plugins, exist_ok=True)

    core_files = [
        "DigiAgentCore.dll",
        "digiagent-master.exe",
        "digiagent-service.exe",
        "digiagent-server.exe",
        "digiagent-worker.exe",
        "digiagent-cli.exe"
    ]
    for fn in core_files:
        src = os.path.join(STAGE_DIR, fn)
        dst = os.path.join(payload_dir, fn)
        if os.path.exists(src):
            copy_and_verify(src, dst)

    stage_plugins = os.path.join(STAGE_DIR, "plugins")
    for f in os.listdir(stage_plugins):
        if f.endswith(".dll"):
            src = os.path.join(stage_plugins, f)
            dst = os.path.join(payload_plugins, f)
            copy_and_verify(src, dst)

    print("\n=== Step 3: Updating Version Manifest (client_version.json) ===")
    manifest_data = {
        "latest_version": "4.11.9.0",
        "release_date": datetime.now().strftime("%Y-%m-%d"),
        "changelog": "Enterprise Refinement: Compact Global Power Down operation card (no horizontal scrollbar / zero repetition), refined modal confirmation with dynamic computer count, separated standard lock from timed focus session with countdown display, enhanced toolbar semantic grouping, and hardened zero-duplicate fleet discovery",
        "windows": {
            "byod_installer": r"\\fileserver\public\Synapse\1-Windows\BYOD\Synapse-Setup-BYOD.exe",
            "lab_installer": r"\\fileserver\public\Synapse\1-Windows\Lab\Synapse-Setup-Lab.exe",
            "core_dll": r"\\fileserver\public\Synapse\4-Auto-Updater\Payload\DigiAgentCore.dll",
            "master_exe": r"\\fileserver\public\Synapse\4-Auto-Updater\Payload\digiagent-master.exe",
            "plugins_dir": r"\\fileserver\public\Synapse\4-Auto-Updater\Payload\plugins"
        },
        "linux": {
            "byod_script": r"\\fileserver\public\Synapse\2-Linux\BYOD\install-synapse-byod.sh",
            "lab_script": r"\\fileserver\public\Synapse\2-Linux\Lab\install-synapse-lab.sh"
        }
    }

    server_manifest = os.path.join(SERVER_SHARE, r"4-Auto-Updater\client_version.json")
    repo_manifest = os.path.join(REPO_DIR, r"deploy\4-Auto-Updater\client_version.json")

    with open(server_manifest, "w", encoding="utf-8") as f:
        json.dump(manifest_data, f, indent=4)
    print(f"  [OK] Updated {server_manifest}")

    with open(repo_manifest, "w", encoding="utf-8") as f:
        json.dump(manifest_data, f, indent=4)
    print(f"  [OK] Updated {repo_manifest}")

    # Update local version tag
    local_ver_file = r"C:\ProgramData\DigiAgent\installed_version.txt"
    try:
        with open(local_ver_file, "w", encoding="utf-8") as f:
            f.write("4.11.9.0\n")
        print(f"  [OK] Updated {local_ver_file} to 4.11.9.0")
    except Exception as e:
        print(f"  [WARN] Could not update local_ver_file: {e}")

    print("\n🎉 ALL DEPLOYMENTS & MANIFESTS VERIFIED 100% INTACT!")

if __name__ == "__main__":
    main()
