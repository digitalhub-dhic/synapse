"""
Synapse BYOD Fleet Manager
Enables the teacher to Release or Connect every device globally,
or Release / Connect any individual student device from this Admin PC.
"""

import os
import sys
import time
import json
import logging
import datetime
import subprocess
import threading

BASE_DIR = r"C:\SynapseAISentinel"
STATE_FILE = os.path.join(BASE_DIR, "byod_state.json")
CLI_PATH = r"C:\Program Files\DigiAgent\digiagent-cli.exe"
MASTER_PASSWORD = "Synapse#Master2026!"

os.makedirs(BASE_DIR, exist_ok=True)

DEFAULT_STATE = {
    "global_mode": "CONNECTED",  # "CONNECTED" (Classroom Active) or "RELEASED" (Class Ended)
    "last_global_change": None,
    "released_devices": {},      # ip -> { "name": ..., "released_at": ... }
    "connected_devices": {}
}


class BYODFleetManager:
    def __init__(self):
        self.state = self.load_state()
        self.computers = self.load_catalog()

    def load_state(self):
        if os.path.exists(STATE_FILE):
            try:
                with open(STATE_FILE, "r", encoding="utf-8") as f:
                    return {**DEFAULT_STATE, **json.load(f)}
            except Exception as e:
                logging.error(f"Error loading BYOD state: {e}")
        return DEFAULT_STATE.copy()

    def save_state(self):
        try:
            with open(STATE_FILE, "w", encoding="utf-8") as f:
                json.dump(self.state, f, indent=2)
        except Exception as e:
            logging.error(f"Error saving BYOD state: {e}")

    def load_catalog(self):
        """Loads all computers from CLI networkobjects dump."""
        try:
            cmd = [CLI_PATH, "networkobjects", "dump"]
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            comps = []
            for line in res.stdout.splitlines():
                parts = [p.strip() for p in line.split("|")]
                if len(parts) >= 6 and parts[3] == "Computer":
                    comp_name = parts[4]
                    ip = parts[5]
                    if ip and ip not in ["192.168.137.1", "192.168.1.150", "127.0.0.1", "computer-50"]:
                        comps.append({"name": comp_name, "ip": ip})
            return comps
        except Exception as e:
            logging.error(f"Error loading catalog: {e}")
            return []

    def is_device_released(self, ip):
        """Checks if a device is currently in RELEASED state."""
        if self.state.get("global_mode") == "RELEASED":
            return True
        return ip in self.state.get("released_devices", {})

    def release_device(self, ip, name=None):
        """Releases a single student device so they can disconnect or use personal Wi-Fi."""
        if not name:
            for c in self.computers:
                if c["ip"] == ip:
                    name = c["name"]
                    break
        name = name or ip
        logging.info(f"🕊️ Releasing BYOD device: {name} ({ip})")

        now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.state["released_devices"][ip] = {
            "name": name,
            "released_at": now_str
        }
        if ip in self.state["connected_devices"]:
            del self.state["connected_devices"][ip]
        self.save_state()

        # Execute remote unlock and release notification
        def _exec_release():
            try:
                # 1. Stop any screen lock
                subprocess.run([CLI_PATH, "feature", "stop", ip, "ScreenLock"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=4)
            except Exception:
                pass

            try:
                # 2. Send friendly release banner to student's screen
                release_text = (
                    "[SYNAPSE BYOD - DEVICE RELEASED]\n\n"
                    "Your laptop has been RELEASED by the instructor.\n"
                    "Class session has ended. You may now disconnect or switch Wi-Fi."
                )
                msg_json = json.dumps({"text": release_text})
                subprocess.run([CLI_PATH, "feature", "start", ip, "TextMessage", msg_json],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=4)
            except Exception:
                pass

            try:
                # 3. If domain reachable, stop service cleanly
                ps_cmd = f"Stop-Service -Name DigiAgentService -Force -ErrorAction SilentlyContinue"
                subprocess.run(["powershell", "-NoProfile", "-Command",
                                f"Invoke-Command -ComputerName {ip} -ScriptBlock {{ {ps_cmd} }} -ErrorAction SilentlyContinue"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            except Exception:
                pass

        threading.Thread(target=_exec_release, daemon=True).start()
        return True

    def connect_device(self, ip, name=None):
        """Connects / engages a single student device into active classroom management."""
        if not name:
            for c in self.computers:
                if c["ip"] == ip:
                    name = c["name"]
                    break
        name = name or ip
        logging.info(f"⚡ Connecting BYOD device: {name} ({ip})")

        now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.state["connected_devices"][ip] = {
            "name": name,
            "connected_at": now_str
        }
        if ip in self.state["released_devices"]:
            del self.state["released_devices"][ip]
        self.save_state()

        # Execute remote engagement
        def _exec_connect():
            try:
                # 1. Start service if stopped
                ps_cmd = f"Start-Service -Name DigiAgentService -ErrorAction SilentlyContinue"
                subprocess.run(["powershell", "-NoProfile", "-Command",
                                f"Invoke-Command -ComputerName {ip} -ScriptBlock {{ {ps_cmd} }} -ErrorAction SilentlyContinue"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            except Exception:
                pass

            try:
                # 2. Stop release text message
                subprocess.run([CLI_PATH, "feature", "stop", ip, "TextMessage"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3)
            except Exception:
                pass

            try:
                # 3. Send welcome notification
                connect_text = (
                    "[SYNAPSE BYOD - CLASS ACTIVE]\n\n"
                    "Connected to classroom session.\n"
                    "Device is now managed by Synapse AI Sentinel."
                )
                msg_json = json.dumps({"text": connect_text})
                subprocess.run([CLI_PATH, "feature", "start", ip, "TextMessage", msg_json],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=4)
                time.sleep(3)
                subprocess.run([CLI_PATH, "feature", "stop", ip, "TextMessage"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3)
            except Exception:
                pass

        threading.Thread(target=_exec_connect, daemon=True).start()
        return True

    def release_all_devices(self):
        """Releases every student device in the classroom at once."""
        logging.info("🕊️ Releasing ALL classroom devices...")
        self.state["global_mode"] = "RELEASED"
        self.state["last_global_change"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.save_state()

        def _worker():
            for comp in self.computers:
                self.release_device(comp["ip"], comp["name"])
        threading.Thread(target=_worker, daemon=True).start()
        return True

    def connect_all_devices(self):
        """Connects and manages every student device in the classroom at once."""
        logging.info("⚡ Connecting ALL classroom devices...")
        self.state["global_mode"] = "CONNECTED"
        self.state["last_global_change"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.state["released_devices"].clear()
        self.save_state()

        def _worker():
            for comp in self.computers:
                self.connect_device(comp["ip"], comp["name"])
        threading.Thread(target=_worker, daemon=True).start()
        return True

    def get_fleet_status(self):
        """Returns the full fleet status list for the dashboard."""
        fleet = []
        global_mode = self.state.get("global_mode", "CONNECTED")

        for comp in self.computers:
            ip = comp["ip"]
            name = comp["name"]
            is_rel = (global_mode == "RELEASED") or (ip in self.state.get("released_devices", {}))
            fleet.append({
                "ip": ip,
                "name": name,
                "status": "RELEASED" if is_rel else "CONNECTED",
                "details": self.state["released_devices"].get(ip, {}).get("released_at", "") if is_rel else "Active Session"
            })
        return {
            "global_mode": global_mode,
            "last_change": self.state.get("last_global_change"),
            "total_devices": len(fleet),
            "released_count": len([d for d in fleet if d["status"] == "RELEASED"]),
            "connected_count": len([d for d in fleet if d["status"] == "CONNECTED"]),
            "devices": fleet
        }


# Singleton instance
byod_manager = BYODFleetManager()

if __name__ == "__main__":
    if len(sys.argv) > 1:
        cmd = sys.argv[1].lower()
        if cmd == "release-all":
            byod_manager.release_all_devices()
            print("[SUCCESS] All classroom devices released.")
        elif cmd == "connect-all":
            byod_manager.connect_all_devices()
            print("[SUCCESS] All classroom devices connected.")
        elif cmd == "release" and len(sys.argv) > 2:
            target_ip = sys.argv[2]
            byod_manager.release_device(target_ip)
            print(f"[SUCCESS] Device {target_ip} released.")
        elif cmd == "connect" and len(sys.argv) > 2:
            target_ip = sys.argv[2]
            byod_manager.connect_device(target_ip)
            print(f"[SUCCESS] Device {target_ip} connected.")
        elif cmd == "status":
            print(json.dumps(byod_manager.get_fleet_status(), indent=2))
        else:
            print("Usage: python byod_fleet_manager.py [release-all | connect-all | release <IP> | connect <IP> | status]")
    else:
        print("Usage: python byod_fleet_manager.py [release-all | connect-all | release <IP> | connect <IP> | status]")
