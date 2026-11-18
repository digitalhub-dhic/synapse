#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Synapse Command — Universal Linux Client Agent
Cross-platform daemon for Linux workstations (Ubuntu, Debian, Fedora, Arch, Mint).
Provides telemetry reporting, remote command execution, screen sharing, and auto-lock watchdog.
"""

import os
import sys
import time
import json
import socket
import logging
import subprocess
import threading
from urllib import request, error

# Default Configuration
DEFAULT_CONFIG = {
    "master_host": "192.168.1.150",
    "master_port": 8080,
    "room_name": "Computer Lab 01",
    "heartbeat_interval_sec": 3,
    "disconnect_lock_timeout_sec": 12,
    "vnc_port": 11100,
    "enable_auto_lock_on_disconnect": True
}

CONFIG_PATH = "/etc/synapse/agent.conf"
LOG_PATH = "/var/log/synapse-agent.log"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(sys.stdout)
    ]
)

def load_config():
    config = DEFAULT_CONFIG.copy()
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                data = json.load(f)
                config.update(data)
                logging.info(f"Loaded configuration from {CONFIG_PATH}")
        except Exception as e:
            logging.warning(f"Error reading {CONFIG_PATH}: {e}. Using defaults.")
    else:
        logging.info("Configuration file not found. Using default parameters.")
    return config

def get_primary_ip():
    """Retrieve primary LAN IPv4 address."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(1.0)
        # Connect to master host or standard gateway
        s.connect(("192.168.1.1", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        try:
            output = subprocess.check_output(["hostname", "-I"], text=True)
            ips = output.strip().split()
            return ips[0] if ips else "127.0.0.1"
        except Exception:
            return "127.0.0.1"

def get_primary_mac():
    """Retrieve primary MAC address."""
    try:
        # Search /sys/class/net/ for non-loopback interface
        net_dir = "/sys/class/net"
        if os.path.exists(net_dir):
            for iface in sorted(os.listdir(net_dir)):
                if iface == "lo" or iface.startswith("veth") or iface.startswith("docker"):
                    continue
                addr_file = os.path.join(net_dir, iface, "address")
                if os.path.exists(addr_file):
                    with open(addr_file, "r") as f:
                        mac = f.read().strip()
                        if mac and mac != "00:00:00:00:00:00":
                            return mac.upper()
    except Exception as e:
        logging.debug(f"Error reading MAC: {e}")
    return "UNKNOWN"

def get_active_gui_user():
    """Detect the real logged-in desktop GUI user (not root service account)."""
    try:
        # 1. Try loginctl
        out = subprocess.check_output(["loginctl", "list-sessions", "--no-legend"], text=True, stderr=subprocess.DEVNULL)
        for line in out.strip().splitlines():
            parts = line.split()
            if len(parts) >= 3:
                user = parts[2]
                if user not in ("root", "gdm", "lightdm", "sddm"):
                    return user
    except Exception:
        pass

    try:
        # 2. Try who / users command
        out = subprocess.check_output(["who"], text=True, stderr=subprocess.DEVNULL)
        for line in out.strip().splitlines():
            parts = line.split()
            if parts and parts[0] != "root":
                return parts[0]
    except Exception:
        pass

    return os.environ.get("SUDO_USER") or "Unassigned"

def get_foreground_app():
    """Detect current active window name using xdotool if available."""
    try:
        # Find active X11 display
        user = get_active_gui_user()
        display = os.environ.get("DISPLAY", ":0")
        env = os.environ.copy()
        env["DISPLAY"] = display

        cmd = ["xdotool", "getactivewindow", "getwindowname"]
        out = subprocess.check_output(cmd, env=env, text=True, stderr=subprocess.DEVNULL, timeout=1.0)
        name = out.strip()
        if name:
            return name[:60]
    except Exception:
        pass
    return "Desktop"

def get_system_metrics():
    """Calculate CPU & RAM percentage from /proc without heavy libraries."""
    cpu_pct = 15
    ram_pct = 45

    # Memory
    try:
        with open("/proc/meminfo", "r") as f:
            lines = f.readlines()
        mem_total = 0
        mem_avail = 0
        for line in lines:
            if line.startswith("MemTotal:"):
                mem_total = int(line.split()[1])
            elif line.startswith("MemAvailable:"):
                mem_avail = int(line.split()[1])
        if mem_total > 0:
            ram_pct = int(round(((mem_total - mem_avail) / mem_total) * 100))
    except Exception:
        pass

    # CPU load average as approximation
    try:
        load = os.getloadavg()[0]
        cores = os.cpu_count() or 1
        cpu_pct = min(100, int(round((load / cores) * 100)))
    except Exception:
        pass

    return cpu_pct, ram_pct

class SynapseLinuxClient:
    def __init__(self, config):
        self.config = config
        self.hostname = socket.gethostname()
        self.mac = get_primary_mac()
        self.is_running = True
        self.is_locked = False
        self.last_master_contact = time.time()
        self.vnc_process = None

    def lock_workstation(self):
        """Immediately locks the active student session."""
        logging.info("LOCK command received: Engaging screen lock.")
        self.is_locked = True
        user = get_active_gui_user()
        display = os.environ.get("DISPLAY", ":0")

        # 1. Try loginctl lock-session
        try:
            subprocess.run(["loginctl", "lock-sessions"], check=False)
        except Exception:
            pass

        # 2. Try xdg-screensaver
        try:
            env = os.environ.copy()
            env["DISPLAY"] = display
            subprocess.run(["xdg-screensaver", "lock"], env=env, check=False)
        except Exception:
            pass

        # 3. If xscreensaver or cinnamon/gnome
        for lock_cmd in [["gnome-screensaver-command", "-l"], ["cinnamon-screensaver-command", "-l"], ["xflock4"]]:
            try:
                subprocess.run(lock_cmd, check=False, stderr=subprocess.DEVNULL)
                break
            except Exception:
                continue

    def unlock_workstation(self):
        """Releases the lock state."""
        logging.info("UNLOCK command received: Releasing screen lock.")
        self.is_locked = False
        try:
            subprocess.run(["loginctl", "unlock-sessions"], check=False)
        except Exception:
            pass

    def show_desktop_notification(self, title, message):
        """Display an administrative broadcast message on the active desktop."""
        logging.info(f"Broadcast Message: [{title}] {message}")
        user = get_active_gui_user()
        display = os.environ.get("DISPLAY", ":0")
        env = os.environ.copy()
        env["DISPLAY"] = display

        # 1. Try zenity popup
        try:
            subprocess.Popen([
                "zenity", "--info", 
                "--title=" + title, 
                "--text=" + message, 
                "--timeout=30"
            ], env=env, stderr=subprocess.DEVNULL)
            return
        except Exception:
            pass

        # 2. Fallback to notify-send
        try:
            subprocess.Popen([
                "notify-send", 
                "-u", "critical", 
                "-i", "dialog-information", 
                title, message
            ], env=env, stderr=subprocess.DEVNULL)
        except Exception:
            pass

    def open_url(self, url):
        """Push a website to open in the active user's default browser."""
        logging.info(f"Opening URL: {url}")
        user = get_active_gui_user()
        display = os.environ.get("DISPLAY", ":0")
        env = os.environ.copy()
        env["DISPLAY"] = display

        try:
            # Run as GUI user using su or runuser if we are root
            if user and user != "root":
                subprocess.Popen(["su", "-", user, "-c", f"DISPLAY={display} xdg-open '{url}'"], stderr=subprocess.DEVNULL)
            else:
                subprocess.Popen(["xdg-open", url], env=env, stderr=subprocess.DEVNULL)
        except Exception as e:
            logging.error(f"Failed to open URL: {e}")

    def execute_shell(self, command):
        """Safely execute remote administrative command."""
        logging.info(f"Executing remote command: {command}")
        try:
            res = subprocess.run(command, shell=True, capture_output=True, text=True, timeout=15)
            logging.info(f"Command exited {res.returncode}: {res.stdout.strip()}")
            return res.stdout
        except Exception as e:
            logging.error(f"Execution error: {e}")
            return str(e)

    def power_action(self, action):
        """Handle reboot or poweroff."""
        logging.info(f"Power action requested: {action}")
        if action == "reboot":
            subprocess.run(["systemctl", "reboot"], check=False)
        elif action == "poweroff" or action == "shutdown":
            subprocess.run(["systemctl", "poweroff"], check=False)

    def start_vnc_stream(self):
        """Ensure x11vnc stream is active on the configured port."""
        if self.vnc_process and self.vnc_process.poll() is None:
            return

        port = self.config.get("vnc_port", 11100)
        display = os.environ.get("DISPLAY", ":0")
        cmd = [
            "x11vnc",
            "-display", display,
            "-rfbport", str(port),
            "-forever",
            "-shared",
            "-noxdamage",
            "-repeat",
            "-q"
        ]
        try:
            self.vnc_process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            logging.info(f"Started VNC stream on port {port}")
        except Exception as e:
            logging.debug(f"Could not start x11vnc: {e}")

    def send_telemetry_heartbeat(self):
        """Send current state to Synapse Master server."""
        master_url = f"http://{self.config['master_host']}:{self.config['master_port']}/api/workstation/heartbeat"
        
        cpu, ram = get_system_metrics()
        ip = get_primary_ip()
        user = get_active_gui_user()
        app = get_foreground_app()

        payload = {
            "name": self.hostname,
            "ip": ip,
            "mac": self.mac,
            "user": user,
            "app": app,
            "cpu": cpu,
            "ram": ram,
            "status": "online",
            "is_locked": self.is_locked,
            "os_type": "linux",
            "timestamp": int(time.time())
        }

        try:
            data = json.dumps(payload).encode("utf-8")
            req = request.Request(master_url, data=data, headers={"Content-Type": "application/json"})
            with request.urlopen(req, timeout=2.5) as resp:
                if resp.status == 200:
                    self.last_master_contact = time.time()
                    body = resp.read().decode("utf-8")
                    try:
                        commands = json.loads(body).get("pending_commands", [])
                        for cmd in commands:
                            self.handle_command(cmd)
                    except Exception:
                        pass
        except Exception as e:
            # Master unreachable
            logging.debug(f"Heartbeat to {master_url} failed: {e}")

    def handle_command(self, cmd_obj):
        """Parse commands dispatched from Synapse Command."""
        action = cmd_obj.get("action")
        payload = cmd_obj.get("payload", "")

        logging.info(f"Received action: {action}")
        if action == "lock":
            self.lock_workstation()
        elif action == "unlock":
            self.unlock_workstation()
        elif action == "message":
            self.show_desktop_notification("Administrator Notice", payload)
        elif action == "open_url":
            self.open_url(payload)
        elif action == "execute":
            self.execute_shell(payload)
        elif action in ("reboot", "poweroff", "shutdown"):
            self.power_action(action)

    def watchdog_loop(self):
        """Autonomous watchdog: auto-locks workstation if disconnected from Master."""
        timeout = self.config.get("disconnect_lock_timeout_sec", 12)
        auto_lock_enabled = self.config.get("enable_auto_lock_on_disconnect", True)

        while self.is_running:
            time.sleep(1.0)
            elapsed = time.time() - self.last_master_contact

            if auto_lock_enabled and elapsed > timeout and not self.is_locked:
                logging.warning(f"Connection lost to Synapse Master for {int(elapsed)}s! Auto-Lock engaged.")
                self.lock_workstation()

    def run(self):
        """Main service loop."""
        logging.info(f"Starting Synapse Linux Agent for {self.hostname} ({self.mac})")
        logging.info(f"Target Master: {self.config['master_host']}:{self.config['master_port']}")

        # Start watchdog in background thread
        watchdog_thread = threading.Thread(target=self.watchdog_loop, daemon=True)
        watchdog_thread.start()

        # Start VNC server for remote management
        self.start_vnc_stream()

        # Telemetry loop
        interval = self.config.get("heartbeat_interval_sec", 3)
        while self.is_running:
            try:
                self.send_telemetry_heartbeat()
            except Exception as e:
                logging.error(f"Error in telemetry heartbeat: {e}")
            time.sleep(interval)

if __name__ == "__main__":
    cfg = load_config()
    client = SynapseLinuxClient(cfg)
    try:
        client.run()
    except KeyboardInterrupt:
        logging.info("Agent stopped by operator.")
