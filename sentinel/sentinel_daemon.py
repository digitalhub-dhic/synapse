"""
Synapse AI Sentinel Daemon & Web Server
Runs the autonomous monitoring loop and hosts the teacher dashboard.
"""

import os
import sys
import time
import json
import ctypes
import logging
import threading
import subprocess
import datetime
from flask import Flask, render_template, jsonify, request, send_from_directory
from PIL import Image
import pygetwindow as gw
import pyautogui

# Single-Instance Enforcement: Prevents duplicate sentinel daemons
_MUTEX_NAME = "Global\\SynapseSentinel_SingleInstance_Mutex"
_mutex_handle = ctypes.windll.kernel32.CreateMutexW(None, False, _MUTEX_NAME)
if ctypes.windll.kernel32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
    sys.exit(0)

sys.path.append(r"C:\SynapseAISentinel")
from synapse_sentinel_engine import (
    sentinel_instance,
    BASE_DIR,
    VIOLATIONS_DIR,
    load_config,
    save_config
)
from byod_fleet_manager import byod_manager

app = Flask(__name__, template_folder=r"C:\SynapseAISentinel\templates", static_folder=r"C:\SynapseAISentinel\static")

# State
daemon_state = {
    "is_active": True,
    "last_scan_time": None,
    "last_window_found": False,
    "scan_count": 0,
    "violations_count": 0,
    "last_error": None
}


def capture_synapse_master():
    """Finds and captures the Synapse Command master window."""
    try:
        # Search all windows for Synapse or DigiAgent Master
        target_win = None
        for w in gw.getAllWindows():
            t = (w.title or "").strip().lower()
            if "synapse" in t or "digiagent master" in t or ("master" in t and "command" in t):
                target_win = w
                break

        if target_win:
            if target_win.isMinimized:
                try:
                    target_win.restore()
                except Exception:
                    pass
            x, y, w, h = target_win.left, target_win.top, target_win.width, target_win.height
            raw_shot = pyautogui.screenshot()
            try:
                img_w, img_h = raw_shot.size
                x1 = max(0, min(x, img_w))
                y1 = max(0, min(y, img_h))
                x2 = max(0, min(x + w, img_w))
                y2 = max(0, min(y + h, img_h))
                if (x2 - x1) >= 600 and (y2 - y1) >= 400:
                    return raw_shot.crop((x1, y1, x2, y2))
                return raw_shot.copy()
            finally:
                raw_shot.close()

        # Fallback: full desktop screenshot if resolution matches master UI
        raw_shot = pyautogui.screenshot()
        try:
            if raw_shot.size[0] >= 1280:
                return raw_shot.copy()
            return None
        finally:
            raw_shot.close()
    except Exception as e:
        daemon_state["last_error"] = str(e)
        return None


def sentinel_worker():
    """Background monitoring thread."""
    logging.info("Sentinel background worker thread started.")
    while daemon_state["is_active"]:
        try:
            cfg = sentinel_instance.config
            if cfg.get("enabled", True):
                master_frame = capture_synapse_master()
                if master_frame is not None:
                    try:
                        daemon_state["last_window_found"] = True
                        daemon_state["last_scan_time"] = datetime.datetime.now().strftime("%H:%M:%S")
                        daemon_state["scan_count"] += 1
                        sentinel_instance.scan_cycle(master_frame)
                    finally:
                        if hasattr(master_frame, "close"):
                            master_frame.close()
                else:
                    daemon_state["last_window_found"] = False
            interval = max(1.0, float(cfg.get("scan_interval_seconds", 2.0)))
            time.sleep(interval)
        except Exception as e:
            logging.error(f"Error in sentinel loop: {e}")
            daemon_state["last_error"] = str(e)
            time.sleep(2.0)


# ==========================================
#              FLASK API ROUTES
# ==========================================

@app.route("/")
def index():
    return render_template("dashboard.html")


@app.route("/api/status")
def get_status():
    cfg = sentinel_instance.config
    locked_list = []
    with sentinel_instance.data_lock:
        locked_items = list(sentinel_instance.locked_hosts.items())
        recent_viols = list(sentinel_instance.recent_violations[:25])

    for ip, info in locked_items:
        comp_name = ip
        for c in sentinel_instance.computers:
            if c["ip"] == ip:
                comp_name = c["name"]
                break
        locked_list.append({
            "ip": ip,
            "name": comp_name,
            "reason": info.get("reason", "Prohibited Activity"),
            "lock_time": datetime.datetime.fromtimestamp(info.get("lock_time", time.time())).strftime("%H:%M:%S")
        })

    return jsonify({
        "enabled": cfg.get("enabled", True),
        "sensitivity": cfg.get("sensitivity", "medium"),
        "targets": cfg.get("detection_targets", {}),
        "total_computers": len(sentinel_instance.computers),
        "locked_count": len(locked_list),
        "locked_computers": locked_list,
        "recent_violations": recent_viols,
        "last_scan_time": daemon_state["last_scan_time"],
        "master_window_found": daemon_state["last_window_found"],
        "scan_count": daemon_state["scan_count"],
        "last_error": daemon_state["last_error"]
    })


@app.route("/api/toggle", methods=["POST"])
def toggle_sentinel():
    cfg = sentinel_instance.config
    cfg["enabled"] = not cfg.get("enabled", True)
    save_config(cfg)
    sentinel_instance.config = cfg
    status_str = "ENABLED" if cfg["enabled"] else "PAUSED"
    logging.info(f"Sentinel toggled by teacher: {status_str}")
    return jsonify({"success": True, "enabled": cfg["enabled"]})


@app.route("/api/config", methods=["POST"])
def update_config():
    data = request.json or {}
    cfg = sentinel_instance.config
    if "sensitivity" in data:
        cfg["sensitivity"] = data["sensitivity"]
    if "detection_targets" in data:
        cfg["detection_targets"] = data["detection_targets"]
    if "play_sound" in data:
        cfg["play_sound"] = bool(data["play_sound"])
    save_config(cfg)
    sentinel_instance.config = cfg
    return jsonify({"success": True, "config": cfg})


@app.route("/api/unlock", methods=["POST"])
def unlock_host():
    data = request.json or {}
    ip = data.get("ip")
    if not ip:
        return jsonify({"success": False, "error": "IP required"}), 400
    res = sentinel_instance.unlock_computer(ip)
    return jsonify({"success": res, "ip": ip})


@app.route("/api/unlock_all", methods=["POST"])
def unlock_all_hosts():
    res = sentinel_instance.unlock_all()
    return jsonify({"success": res})


# ==========================================
#         BYOD FLEET MANAGEMENT ROUTES
# ==========================================

@app.route("/api/fleet")
def get_fleet():
    return jsonify(byod_manager.get_fleet_status())


@app.route("/api/fleet/release_all", methods=["POST"])
def fleet_release_all():
    res = byod_manager.release_all_devices()
    return jsonify({"success": res})


@app.route("/api/fleet/connect_all", methods=["POST"])
def fleet_connect_all():
    res = byod_manager.connect_all_devices()
    return jsonify({"success": res})


@app.route("/api/fleet/release_device", methods=["POST"])
def fleet_release_device():
    data = request.json or {}
    ip = data.get("ip")
    if not ip:
        return jsonify({"success": False, "error": "IP required"}), 400
    res = byod_manager.release_device(ip)
    return jsonify({"success": res, "ip": ip})


@app.route("/api/fleet/connect_device", methods=["POST"])
def fleet_connect_device():
    data = request.json or {}
    ip = data.get("ip")
    if not ip:
        return jsonify({"success": False, "error": "IP required"}), 400
    res = byod_manager.connect_device(ip)
    return jsonify({"success": res, "ip": ip})



@app.route("/api/evidence/<path:filepath>")
def serve_evidence(filepath):
    # Sanitize and serve from VIOLATIONS_DIR
    full_path = os.path.normpath(os.path.join(VIOLATIONS_DIR, filepath))
    if not full_path.startswith(os.path.abspath(VIOLATIONS_DIR)):
        return "Access denied", 403
    dir_name = os.path.dirname(full_path)
    file_name = os.path.basename(full_path)
    return send_from_directory(dir_name, file_name)


if __name__ == "__main__":
    # Start background loop
    t = threading.Thread(target=sentinel_worker, daemon=True)
    t.start()
    logging.info("Starting Synapse AI Sentinel Web Dashboard on port 5050...")
    app.run(host="127.0.0.1", port=5050, debug=False, threaded=True)
