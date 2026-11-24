"""
Synapse App Update & Git Auto-Sync Daemon
Monitors app updates, staged payloads, and build source modifications in real time.
Whenever an app update is made or deployed, git is automatically synced and pushed.
"""

import os
import sys
import time
import subprocess
from datetime import datetime

WATCH_TARGETS = [
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\client_version.json",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\DigiAgentCore.dll",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\plugins\screenlock.dll",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\digiagent-master.exe",
    r"C:\Program Files\DigiAgent\DigiAgentCore.dll",
    r"C:\Program Files\DigiAgent\custom_theme.qss",
    r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build\core\src\LockWidget.cpp",
    r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build\plugins\screenlock\ScreenLockFeaturePlugin.cpp",
    r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build\master\src\ComputerSelectPanel.cpp",
    r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build\master\src\ComputerInspectorPanel.cpp",
]

SYNC_SCRIPT = r"C:\Users\administrator\projects\synapse\sync_and_push_to_git.py"
LOG_FILE = r"C:\Users\administrator\projects\synapse\git_autosync.log"

def log(msg):
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    entry = f"[{ts}] {msg}"
    print(entry)
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(entry + "\n")
    except Exception:
        pass

def get_snapshot():
    snapshot = {}
    for path in WATCH_TARGETS:
        try:
            if os.path.exists(path):
                stat = os.stat(path)
                snapshot[path] = (stat.st_mtime, stat.st_size)
        except Exception:
            pass
    return snapshot

def run_sync():
    log("App update detected! Triggering automated Git synchronization...")
    try:
        p = subprocess.run([sys.executable, SYNC_SCRIPT], capture_output=True, text=True)
        log(f"Sync output:\n{p.stdout.strip()}")
        if p.stderr:
            log(f"Sync stderr:\n{p.stderr.strip()}")
    except Exception as e:
        log(f"Failed to execute sync script: {e}")

def main():
    log("Synapse Git Auto-Sync Watcher Daemon started.")
    prev_snapshot = get_snapshot()

    while True:
        try:
            time.sleep(5)
            curr_snapshot = get_snapshot()
            changed = False
            for p, val in curr_snapshot.items():
                if p not in prev_snapshot or prev_snapshot[p] != val:
                    log(f"Detected change in: {p}")
                    changed = True
                    break

            if changed:
                time.sleep(3) # debounce
                run_sync()
                prev_snapshot = get_snapshot()
        except KeyboardInterrupt:
            break
        except Exception as e:
            log(f"Watcher loop error: {e}")
            time.sleep(10)

if __name__ == "__main__":
    main()
