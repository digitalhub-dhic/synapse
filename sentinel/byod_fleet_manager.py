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
import ipaddress
import uuid
import tempfile

BASE_DIR = r"C:\SynapseAISentinel"
STATE_FILE = os.path.join(BASE_DIR, "byod_state.json")
CLI_PATH = r"C:\Program Files\DigiAgent\digiagent-cli.exe"

os.makedirs(BASE_DIR, exist_ok=True)

DEFAULT_STATE = {
    "global_mode": "UNVERIFIED",  # Request progress only; clients do not report an acknowledged state yet.
    "last_global_change": None,
    "released_devices": {},      # ip -> { "name": ..., "released_at": ... }
    "connected_devices": {},
    "pending_operations": {}     # ip -> {"desired": ..., "status": ..., "details": ...}
}


class BYODFleetManager:
    def __init__(self):
        self.state_lock = threading.RLock()
        self.state = self.load_state()
        self.computers = self.load_catalog()
        self.catalog_loaded_at = time.monotonic()

    def refresh_catalog(self, force=False):
        """Refresh enrolled targets periodically so DHCP/discovery changes apply live."""
        if force or time.monotonic() - self.catalog_loaded_at >= 5:
            self.computers = self.load_catalog()
            self.catalog_loaded_at = time.monotonic()

    def load_state(self):
        if os.path.exists(STATE_FILE):
            try:
                with open(STATE_FILE, "r", encoding="utf-8") as f:
                    loaded = json.load(f)
                if not isinstance(loaded, dict):
                    raise ValueError("BYOD state root must be a JSON object")
                state = {**DEFAULT_STATE, **loaded}
                for key in ("released_devices", "connected_devices", "pending_operations"):
                    if not isinstance(state.get(key), dict):
                        raise ValueError(f"BYOD state field {key} must be an object")
                return state
            except Exception as e:
                logging.error(f"Error loading BYOD state: {e}")
        # Round-trip through JSON to avoid sharing nested dictionaries between
        # the module default and this manager instance.
        return json.loads(json.dumps(DEFAULT_STATE))

    def save_state(self):
        """Persist a complete snapshot atomically; report failure to callers."""
        temporary_path = None
        try:
            with self.state_lock:
                directory = os.path.dirname(STATE_FILE)
                os.makedirs(directory, exist_ok=True)
                descriptor, temporary_path = tempfile.mkstemp(
                    prefix=".byod_state_", suffix=".tmp", dir=directory
                )
                with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as f:
                    json.dump(self.state, f, indent=2)
                    f.flush()
                    os.fsync(f.fileno())
                os.replace(temporary_path, STATE_FILE)
                temporary_path = None
            return True
        except Exception as e:
            logging.error(f"Error saving BYOD state: {e}")
            return False
        finally:
            if temporary_path and os.path.exists(temporary_path):
                try:
                    os.unlink(temporary_path)
                except OSError:
                    pass

    def load_catalog(self):
        """Loads all computers from CLI networkobjects dump."""
        try:
            cmd = [CLI_PATH, "networkobjects", "dump"]
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            if res.returncode != 0:
                raise RuntimeError(res.stderr.strip() or "network object catalog query failed")
            comps = []
            seen = set()
            for line in res.stdout.splitlines():
                parts = [p.strip() for p in line.split("|")]
                if len(parts) >= 6 and parts[3] == "Computer":
                    comp_name, raw_ip = parts[4], parts[5]
                    try:
                        ip = str(ipaddress.IPv4Address(raw_ip))
                    except ipaddress.AddressValueError:
                        continue
                    if ip not in ("192.168.137.1", "192.168.1.150", "127.0.0.1") and ip not in seen:
                        comps.append({"name": comp_name[:128], "ip": ip})
                        seen.add(ip)
            return comps
        except Exception as e:
            logging.error(f"Error loading catalog: {e}")
            return []

    def is_device_released(self, ip):
        """Only treat a release as real when an explicit client acknowledgement exists."""
        record = self.state.get("released_devices", {}).get(ip, {})
        return isinstance(record, dict) and record.get("confirmed") is True

    def _run_feature(self, ip, feature, arguments=None, timeout=8):
        command = [CLI_PATH, "feature", "start", ip, feature]
        if arguments is not None:
            command.append(arguments)
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
            return result.returncode == 0, (result.stderr or result.stdout or "").strip()[:300]
        except (OSError, subprocess.TimeoutExpired) as exc:
            return False, str(exc)[:300]

    def _run_remote_service_command(self, ip, action, timeout=45):
        """Start/stop the client service through configured PowerShell remoting."""
        if action == "start":
            script_block = (
                "Start-Service -Name 'DigiAgentService' -ErrorAction Stop; "
                "(Get-Service -Name 'DigiAgentService').WaitForStatus('Running', [TimeSpan]::FromSeconds(30))"
            )
        elif action == "stop":
            script_block = (
                "Stop-Service -Name 'DigiAgentService' -Force -ErrorAction Stop; "
                "(Get-Service -Name 'DigiAgentService').WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))"
            )
        else:
            raise ValueError("Unsupported remote service action")

        command = (
            "$ErrorActionPreference='Stop'; try { "
            f"Invoke-Command -ComputerName '{ip}' -ScriptBlock {{ {script_block} }} -ErrorAction Stop "
            "} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }"
        )
        try:
            result = subprocess.run(
                ["powershell", "-NoProfile", "-NonInteractive", "-Command", command],
                capture_output=True, text=True, timeout=timeout
            )
            return result.returncode == 0, (result.stderr or result.stdout or "").strip()[:300]
        except (OSError, subprocess.TimeoutExpired) as exc:
            return False, str(exc)[:300]

    def release_device(self, ip, name=None):
        """Releases a single student device so they can disconnect or use personal Wi-Fi."""
        ip = self.validate_target(ip)
        if ip is None:
            return False
        with self.state_lock:
            if self.state.get("global_mode", "CONNECTED") not in ("RELEASING", "CONNECTING"):
                self.state["global_mode"] = "MIXED"
        if not name:
            for c in self.computers:
                if c["ip"] == ip:
                    name = c["name"]
                    break
        name = name or ip
        logging.info(f"🕊️ Releasing BYOD device: {name} ({ip})")

        now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        operation_id = uuid.uuid4().hex
        with self.state_lock:
            self.state.setdefault("pending_operations", {})[ip] = {
                "id": operation_id, "desired": "RELEASED", "status": "PENDING",
                "details": "Sending release command", "updated_at": now_str
            }
        if not self.save_state():
            with self.state_lock:
                self.state["pending_operations"][ip].update(
                    status="FAILED", details="Could not persist operation state; release was not sent"
                )
            return False

        # Execute remote unlock and release notification
        def _exec_release():
            try:
                # 1. Stop any screen lock
                subprocess.run([CLI_PATH, "feature", "stop", ip, "ScreenLock"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=4)
            except Exception:
                pass

            # Persist release state on the client so its BYOD guards stop
            # enforcing during an approved leave period.
            release_cmd = "Set-Content -Path 'C:\\ProgramData\\DigiAgent\\byod_policy.json' -Value '{\"released\":true}' -Encoding UTF8 -Force"
            # Veyon/DigiAgent's StartApp feature expects an applications array.
            # A scalar string is rejected by the feature argument parser.
            release_args = json.dumps({"applications": ["powershell.exe -NoProfile -NonInteractive -Command " + release_cmd]})
            accepted, detail = self._run_feature(ip, "StartApp", release_args)
            with self.state_lock:
                operation = self.state.setdefault("pending_operations", {}).get(ip, {})
                if operation.get("id") != operation_id:
                    logging.info("Ignoring stale release result for %s", ip)
                    return
                if accepted:
                    # A successful teacher-side CLI exit only means the request
                    # was accepted. It is not proof that the client wrote the
                    # protected policy file or stopped enforcing controls.
                    operation.update(status="REQUESTED", details="Release request accepted by the classroom service; client state is unconfirmed")
                else:
                    operation.update(status="FAILED", details=detail or "Teacher agent did not accept the release command")
                operation["updated_at"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            if not self.save_state():
                logging.error("Could not persist release result for %s", ip)

            if not accepted:
                logging.error("Release command failed for %s (%s): %s", name, ip, detail)
                return

            try:
                # 2. Send friendly release banner to student's screen
                release_text = (
                    "[SYNAPSE BYOD - RELEASE REQUEST]\n\n"
                    "Your instructor submitted a release request. The device has not yet confirmed its state.\n"
                    "Please wait for instructor confirmation before disconnecting or switching networks."
                )
                msg_json = json.dumps({"text": release_text})
                subprocess.run([CLI_PATH, "feature", "start", ip, "TextMessage", msg_json],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=4)
            except Exception:
                pass

            stopped, service_detail = self._run_remote_service_command(ip, "stop")
            if not stopped:
                with self.state_lock:
                    operation = self.state.setdefault("pending_operations", {}).get(ip, {})
                    if operation.get("id") == operation_id and operation.get("status") == "REQUESTED":
                        operation["details"] = (
                            "Release request accepted; service stop was not confirmed"
                            + (f": {service_detail}" if service_detail else "")
                        )
                        operation["updated_at"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                if not self.save_state():
                    logging.error("Could not persist release service status for %s", ip)

        # Non-daemon threads keep one-shot CLI invocations alive until the
        # remote request has at least been submitted.
        threading.Thread(target=_exec_release, daemon=False).start()
        return True

    def connect_device(self, ip, name=None):
        """Connects / engages a single student device into active classroom management."""
        ip = self.validate_target(ip)
        if ip is None:
            return False
        with self.state_lock:
            if self.state.get("global_mode", "CONNECTED") not in ("RELEASING", "CONNECTING"):
                self.state["global_mode"] = "MIXED"
        if not name:
            for c in self.computers:
                if c["ip"] == ip:
                    name = c["name"]
                    break
        name = name or ip
        logging.info(f"⚡ Connecting BYOD device: {name} ({ip})")

        now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        operation_id = uuid.uuid4().hex
        with self.state_lock:
            self.state.setdefault("pending_operations", {})[ip] = {
                "id": operation_id, "desired": "CONNECTED", "status": "PENDING",
                "details": "Sending connect command", "updated_at": now_str
            }
        if not self.save_state():
            with self.state_lock:
                self.state["pending_operations"][ip].update(
                    status="FAILED", details="Could not persist operation state; connect was not sent"
                )
            return False

        # Execute remote engagement
        def _exec_connect():
            # Release shuts down the Veyon service, so restore the feature
            # channel before asking it to remove the released policy.
            service_started, service_detail = self._run_remote_service_command(ip, "start")
            if not service_started:
                with self.state_lock:
                    operation = self.state.setdefault("pending_operations", {}).get(ip, {})
                    if operation.get("id") != operation_id:
                        logging.info("Ignoring stale connect result for %s", ip)
                        return
                    operation.update(
                        status="FAILED",
                        details="Could not start the classroom service; connect command was not sent"
                        + (f": {service_detail}" if service_detail else "")
                    )
                    operation["updated_at"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                if not self.save_state():
                    logging.error("Could not persist connect service failure for %s", ip)
                return

            # Clear release policy through the still-running teacher agent.
            revoke_cmd = "Remove-Item -Path 'C:\\ProgramData\\DigiAgent\\byod_policy.json' -Force -ErrorAction SilentlyContinue"
            revoke_args = json.dumps({"applications": ["powershell.exe -NoProfile -NonInteractive -Command " + revoke_cmd]})
            accepted, detail = self._run_feature(ip, "StartApp", revoke_args)
            with self.state_lock:
                operation = self.state.setdefault("pending_operations", {}).get(ip, {})
                if operation.get("id") != operation_id:
                    logging.info("Ignoring stale connect result for %s", ip)
                    return
                if accepted:
                    operation.update(status="REQUESTED", details="Connect request accepted by the classroom service; client state is unconfirmed")
                else:
                    operation.update(status="FAILED", details=detail or "Teacher agent did not accept the connect command")
                operation["updated_at"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            if not self.save_state():
                logging.error("Could not persist connect result for %s", ip)

            if not accepted:
                logging.error("Connect command failed for %s (%s): %s", name, ip, detail)
                return

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

        threading.Thread(target=_exec_connect, daemon=False).start()
        return True

    def release_all_devices(self):
        """Releases every student device in the classroom at once."""
        self.refresh_catalog(force=True)
        if not self.computers:
            logging.error("Release-all rejected because the classroom catalog is empty.")
            return False
        logging.info("🕊️ Releasing ALL classroom devices...")
        with self.state_lock:
            self.state["global_mode"] = "RELEASING"
            self.state["last_global_change"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        if not self.save_state():
            logging.error("Release-all rejected because the request state could not be persisted.")
            return False

        def _worker():
            for comp in self.computers:
                self.release_device(comp["ip"], comp["name"])
        threading.Thread(target=_worker, daemon=False).start()
        return True

    def connect_all_devices(self):
        """Connects and manages every student device in the classroom at once."""
        self.refresh_catalog(force=True)
        if not self.computers:
            logging.error("Connect-all rejected because the classroom catalog is empty.")
            return False
        logging.info("⚡ Connecting ALL classroom devices...")
        with self.state_lock:
            self.state["global_mode"] = "CONNECTING"
            self.state["last_global_change"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        if not self.save_state():
            logging.error("Connect-all rejected because the request state could not be persisted.")
            return False

        def _worker():
            for comp in self.computers:
                self.connect_device(comp["ip"], comp["name"])
        threading.Thread(target=_worker, daemon=False).start()
        return True

    def get_fleet_status(self):
        """Returns the full fleet status list for the dashboard."""
        self.refresh_catalog()
        fleet = []
        global_mode = self.state.get("global_mode", "UNVERIFIED")

        for comp in self.computers:
            ip = comp["ip"]
            name = comp["name"]
            operation = self.state.get("pending_operations", {}).get(ip, {})
            if operation.get("status") == "PENDING":
                status = "RELEASE_PENDING" if operation.get("desired") == "RELEASED" else "CONNECT_PENDING"
                details = operation.get("details", "Command pending")
            elif operation.get("status") == "FAILED":
                status, details = "ERROR", operation.get("details", "Command failed")
            elif operation.get("status") == "REQUESTED":
                status = "RELEASE_REQUESTED" if operation.get("desired") == "RELEASED" else "CONNECT_REQUESTED"
                details = operation.get("details", "Command accepted")
            elif self.is_device_released(ip):
                status, details = "RELEASED", self.state["released_devices"].get(ip, {}).get("released_at", "")
            else:
                status, details = "ENROLLED", "Enrolled; live management state is unverified"
            fleet.append({
                "ip": ip,
                "name": name,
                "status": status,
                "desired": operation.get("desired", "RELEASED" if self.is_device_released(ip) else "CONNECTED"),
                "details": details
            })
        released_count = len([d for d in fleet if d["status"] in ("RELEASED", "RELEASE_REQUESTED")])
        pending_count = len([d for d in fleet if d["status"] in ("RELEASE_PENDING", "CONNECT_PENDING")])
        failed_count = len([d for d in fleet if d["status"] == "ERROR"])
        connect_request_count = len([
            d for d in fleet
            if d["status"] in ("CONNECT_REQUESTED", "CONNECT_PENDING")
        ])
        return {
            "global_mode": global_mode,
            "last_change": self.state.get("last_global_change"),
            "total_devices": len(fleet),
            "released_count": released_count,
            "connected_count": 0,  # No client acknowledgment is currently implemented.
            "connect_request_count": connect_request_count,
            "pending_count": pending_count,
            "failed_count": failed_count,
            "devices": fleet
        }

    def validate_target(self, value):
        """Only allow enrolled IPv4 endpoints as fleet control targets."""
        self.refresh_catalog()
        try:
            normalized = str(ipaddress.IPv4Address(value))
        except (ipaddress.AddressValueError, TypeError):
            logging.warning("Rejected invalid fleet target: %r", value)
            return None
        if normalized not in {device["ip"] for device in self.computers}:
            logging.warning("Rejected non-enrolled fleet target: %s", normalized)
            return None
        return normalized


# Singleton instance
byod_manager = BYODFleetManager()

if __name__ == "__main__":
    if len(sys.argv) > 1:
        cmd = sys.argv[1].lower()
        if cmd == "release-all":
            if not byod_manager.release_all_devices():
                sys.exit(1)
            print("[QUEUED] Release requests submitted; check per-device status for failures.")
        elif cmd == "connect-all":
            if not byod_manager.connect_all_devices():
                sys.exit(1)
            print("[QUEUED] Connect requests submitted; check per-device status for failures.")
        elif cmd == "release" and len(sys.argv) > 2:
            target_ip = sys.argv[2]
            if not byod_manager.release_device(target_ip):
                sys.exit(1)
            print(f"[QUEUED] Release request submitted for {target_ip}.")
        elif cmd == "connect" and len(sys.argv) > 2:
            target_ip = sys.argv[2]
            if not byod_manager.connect_device(target_ip):
                sys.exit(1)
            print(f"[QUEUED] Connect request submitted for {target_ip}.")
        elif cmd == "status":
            print(json.dumps(byod_manager.get_fleet_status(), indent=2))
        else:
            print("Usage: python byod_fleet_manager.py [release-all | connect-all | release <IP> | connect <IP> | status]")
    else:
        print("Usage: python byod_fleet_manager.py [release-all | connect-all | release <IP> | connect <IP> | status]")
