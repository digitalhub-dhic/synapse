"""
Synapse Git Auto-Sync Engine
Automatically syncs all app modifications, core patches, deployment scripts,
and version manifests into the git repository and pushes directly to GitHub.
"""

import os
import sys
import shutil
import subprocess
import json
from datetime import datetime

REPO_DIR = r"C:\Users\administrator\projects\synapse"
BUILD_SRC_ROOT = r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build"
PROGRAM_FILES = r"C:\Program Files\DigiAgent"
SERVER_SHARE = r"\\192.168.1.50\public\Synapse"

# File mappings: (source_path, repo_dest_path)
SYNC_MAPPINGS = [
    # Core patches & UI
    (os.path.join(BUILD_SRC_ROOT, r"core\src\LockWidget.cpp"),
     os.path.join(REPO_DIR, r"core-patches\LockWidget.cpp")),
    (os.path.join(BUILD_SRC_ROOT, r"plugins\screenlock\ScreenLockFeaturePlugin.cpp"),
     os.path.join(REPO_DIR, r"core-patches\ScreenLockFeaturePlugin.cpp")),
    (os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerSelectPanel.cpp"),
     os.path.join(REPO_DIR, r"core-patches\ComputerSelectPanel.cpp")),
    (os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerInspectorPanel.cpp"),
     os.path.join(REPO_DIR, r"core-patches\ComputerInspectorPanel.cpp")),
    (os.path.join(BUILD_SRC_ROOT, r"master\src\CheckableItemProxyModel.cpp"),
     os.path.join(REPO_DIR, r"core-patches\CheckableItemProxyModel.cpp")),
    (os.path.join(BUILD_SRC_ROOT, r"plugins\platform\windows\WindowsInputDeviceFunctions.cpp"),
     os.path.join(REPO_DIR, r"core-patches\WindowsInputDeviceFunctions.cpp")),
    (os.path.join(BUILD_SRC_ROOT, r"plugins\platform\windows\WindowsInputDeviceFunctions.h"),
     os.path.join(REPO_DIR, r"core-patches\WindowsInputDeviceFunctions.h")),
    (os.path.join(PROGRAM_FILES, "custom_theme.qss"),
     os.path.join(REPO_DIR, r"core-patches\custom_theme.qss")),
    (os.path.join(PROGRAM_FILES, "synapse_white_green.qss"),
     os.path.join(REPO_DIR, r"core-patches\synapse_white_green.qss")),

    # Auto-Updater & deployment manifests
    (os.path.join(SERVER_SHARE, r"4-Auto-Updater\client_version.json"),
     os.path.join(REPO_DIR, r"deploy\4-Auto-Updater\client_version.json")),
    (os.path.join(SERVER_SHARE, r"4-Auto-Updater\Check-And-AutoUpdate.ps1"),
     os.path.join(REPO_DIR, r"deploy\4-Auto-Updater\Check-And-AutoUpdate.ps1")),
    (os.path.join(SERVER_SHARE, r"4-Auto-Updater\Payload\client_config.json"),
     os.path.join(REPO_DIR, r"deploy\4-Auto-Updater\Payload\client_config.json")),

    # Linux scripts
    (os.path.join(SERVER_SHARE, r"2-Linux\BYOD\install-synapse-byod.sh"),
     os.path.join(REPO_DIR, r"deploy\2-Linux\BYOD\install-synapse-byod.sh")),
    (os.path.join(SERVER_SHARE, r"2-Linux\Lab\install-synapse-lab.sh"),
     os.path.join(REPO_DIR, r"deploy\2-Linux\Lab\install-synapse-lab.sh")),
]

def run_cmd(cmd_list, cwd=REPO_DIR):
    try:
        p = subprocess.run(cmd_list, cwd=cwd, capture_output=True, text=True, check=True)
        return p.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"[ERR] Command failed: {' '.join(cmd_list)}")
        if e.stdout:
            print(f"Stdout: {e.stdout.strip()}")
        if e.stderr:
            print(f"Stderr: {e.stderr.strip()}")
        return None

def sync_files():
    copied = 0
    for src, dst in SYNC_MAPPINGS:
        if os.path.exists(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            # Compare contents
            should_copy = False
            if not os.path.exists(dst):
                should_copy = True
            else:
                try:
                    with open(src, 'rb') as f1, open(dst, 'rb') as f2:
                        if f1.read() != f2.read():
                            should_copy = True
                except Exception:
                    should_copy = True
            
            if should_copy:
                shutil.copy2(src, dst)
                copied += 1
                print(f"[SYNC] Copied: {src} -> {dst}")
        else:
            print(f"[SKIP] Source path not accessible: {src}")
    return copied

def get_latest_version():
    candidates = [
        os.path.join(SERVER_SHARE, r"4-Auto-Updater\client_version.json"),
        os.path.join(REPO_DIR, r"deploy\4-Auto-Updater\client_version.json"),
    ]
    for c in candidates:
        if os.path.exists(c):
            try:
                with open(c, 'r', encoding='utf-8-sig', errors='ignore') as f:
                    data = json.load(f)
                    return data.get('latest_version', 'latest'), data.get('changelog', '')
            except Exception as e:
                print(f"[WARN] Error reading version from {c}: {e}")
    return "update", ""

def commit_and_push():
    status = run_cmd(["git", "status", "--porcelain"])
    if not status:
        print("[OK] Git working directory is clean. No app changes to push.")
        return False

    print("[*] Detected modifications in Synapse Git Repository:")
    print(status)

    version, changelog = get_latest_version()
    now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    commit_msg = f"App update: Synapse v{version} ({now_str})"
    if changelog:
        commit_msg += f"\n\nRelease Notes: {changelog}"

    # Stage all changes
    run_cmd(["git", "add", "-A"])

    # Commit
    commit_res = run_cmd(["git", "commit", "-m", commit_msg])
    if commit_res:
        print(f"[OK] Committed changes:\n{commit_res}")

    # Push to origin main
    push_res = run_cmd(["git", "push", "origin", "main"])
    if push_res is not None:
        print(f"[SUCCESS] Pushed update v{version} to GitHub origin/main successfully!")
        return True
    else:
        print("[ERR] Push failed. Check network or credentials.")
        return False

def main():
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Checking Synapse app updates for Git...")
    sync_files()
    commit_and_push()

if __name__ == "__main__":
    main()
