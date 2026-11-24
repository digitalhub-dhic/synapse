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

BUILD_SRC_ROOT = r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4\work\DigiAgent-build"

WATCH_TARGETS = [
    # Server payloads & auto-updater
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\client_version.json",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Check-And-AutoUpdate.ps1",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\DigiAgentCore.dll",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\plugins\screenlock.dll",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\plugins\powercontrol.dll",
    r"\\192.168.1.50\public\Synapse\4-Auto-Updater\Payload\digiagent-master.exe",

    # Installed app binaries and QSS
    r"C:\Program Files\DigiAgent\digiagent-master.exe",
    r"C:\Program Files\DigiAgent\DigiAgentCore.dll",
    r"C:\Program Files\DigiAgent\plugins\powercontrol.dll",
    r"C:\Program Files\DigiAgent\plugins\screenlock.dll",
    r"C:\Program Files\DigiAgent\custom_theme.qss",
    r"C:\Program Files\DigiAgent\synapse_white_green.qss",

    # Key build source files
    os.path.join(BUILD_SRC_ROOT, r"core\src\LockWidget.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"core\src\LockWidget.h"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\screenlock\ScreenLockFeaturePlugin.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\screenlock\ScreenLockFeaturePlugin.h"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\powercontrol\PowerDownTimeInputDialog.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\powercontrol\PowerDownTimeInputDialog.h"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\powercontrol\PowerControlFeaturePlugin.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\powercontrol\PowerControlFeaturePlugin.h"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerSelectPanel.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerSelectPanel.h"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerInspectorPanel.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerInspectorPanel.h"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\CheckableItemProxyModel.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\CheckableItemProxyModel.h"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerItemDelegate.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerItemDelegate.h"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerMonitoringView.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\ComputerMonitoringView.h"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\MainWindow.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"master\src\MainWindow.h"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\platform\windows\WindowsInputDeviceFunctions.cpp"),
    os.path.join(BUILD_SRC_ROOT, r"plugins\platform\windows\WindowsInputDeviceFunctions.h"),
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
