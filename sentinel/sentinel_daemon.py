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
from collections import deque
from flask import Flask, render_template, jsonify, request, send_from_directory, abort, redirect, url_for, session
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
from sentinel_auth import get_security_dir, load_security_files, verify_console_password

try:
    _sentinel_log_dir = get_security_dir().parent
    _sentinel_log_dir.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(
        filename=str(_sentinel_log_dir / "sentinel.log"),
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
except OSError:
    logging.basicConfig(level=logging.INFO)

app = Flask(__name__, template_folder=r"C:\SynapseAISentinel\templates", static_folder=r"C:\SynapseAISentinel\static")
app.config.update(
    SESSION_COOKIE_HTTPONLY=True,
    SESSION_COOKIE_SAMESITE="Strict",
    SESSION_COOKIE_SECURE=False,  # The UI is loopback-only HTTP; Secure cookies would not be sent.
    PERMANENT_SESSION_LIFETIME=datetime.timedelta(hours=8),
    MAX_CONTENT_LENGTH=16 * 1024,
)

CONSOLE_AUTH_READY = False
CONSOLE_CREDENTIAL = None
try:
    session_secret, CONSOLE_CREDENTIAL = load_security_files()
    app.secret_key = session_secret
    CONSOLE_AUTH_READY = True
except (OSError, ValueError, TypeError, KeyError) as exc:
    logging.error("Sentinel console authentication is not configured: %s", exc)

_LOGIN_FAILURES = deque()
_LOGIN_FAILURES_LOCK = threading.Lock()
_LOGIN_LOCKOUT_UNTIL = 0.0
_LOGIN_FAILURE_WINDOW_SECONDS = 600
_LOGIN_LOCKOUT_SECONDS = 900
_LOGIN_MAX_FAILURES = 5


@app.before_request
def require_authenticated_local_console():
    """Require a teacher session and same-origin control requests on loopback."""
    if request.remote_addr not in ("127.0.0.1", "::1"):
        abort(403)
    if not CONSOLE_AUTH_READY:
        return render_template("auth_setup_required.html"), 503

    if request.endpoint == "login":
        if request.method not in ("GET", "HEAD"):
            origin = request.headers.get("Origin", "")
            if origin.rstrip("/") != request.host_url.rstrip("/"):
                abort(403)
        return None

    if not session.get("teacher_authenticated"):
        if request.path.startswith("/api/"):
            return jsonify({"success": False, "error": "Teacher login required"}), 401
        return redirect(url_for("login"))

    if request.method not in ("GET", "HEAD"):
        origin = request.headers.get("Origin", "")
        if origin.rstrip("/") != request.host_url.rstrip("/"):
            abort(403)
    return None


@app.after_request
def add_private_console_headers(response):
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Referrer-Policy"] = "no-referrer"
    response.headers["Cache-Control"] = "no-store"
    return response


def _login_is_locked_out():
    global _LOGIN_LOCKOUT_UNTIL
    now = time.monotonic()
    with _LOGIN_FAILURES_LOCK:
        if now < _LOGIN_LOCKOUT_UNTIL:
            return True
        while _LOGIN_FAILURES and now - _LOGIN_FAILURES[0] > _LOGIN_FAILURE_WINDOW_SECONDS:
            _LOGIN_FAILURES.popleft()
        return len(_LOGIN_FAILURES) >= _LOGIN_MAX_FAILURES


def _record_login_failure():
    global _LOGIN_LOCKOUT_UNTIL
    now = time.monotonic()
    with _LOGIN_FAILURES_LOCK:
        while _LOGIN_FAILURES and now - _LOGIN_FAILURES[0] > _LOGIN_FAILURE_WINDOW_SECONDS:
            _LOGIN_FAILURES.popleft()
        _LOGIN_FAILURES.append(now)
        if len(_LOGIN_FAILURES) >= _LOGIN_MAX_FAILURES:
            _LOGIN_LOCKOUT_UNTIL = now + _LOGIN_LOCKOUT_SECONDS
            _LOGIN_FAILURES.clear()


@app.route("/login", methods=["GET", "POST"])
def login():
    if session.get("teacher_authenticated"):
        return redirect(url_for("index"))
    if request.method == "GET":
        return render_template("login.html", error=None)
    if _login_is_locked_out():
        return render_template("login.html", error="Too many failed attempts. Try again in 15 minutes."), 429

    password = request.form.get("password", "")
    if verify_console_password(password, CONSOLE_CREDENTIAL):
        with _LOGIN_FAILURES_LOCK:
            _LOGIN_FAILURES.clear()
        session.clear()
        session.permanent = True
        session["teacher_authenticated"] = True
        return redirect(url_for("index"))

    _record_login_failure()
    return render_template("login.html", error="Password was not accepted."), 401


@app.route("/logout", methods=["POST"])
def logout():
    session.clear()
    return redirect(url_for("login"))

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
        if data["sensitivity"] not in ("low", "medium", "strict"):
            return jsonify({"success": False, "error": "Invalid sensitivity"}), 400
        cfg["sensitivity"] = data["sensitivity"]
    if "detection_targets" in data:
        if not isinstance(data["detection_targets"], dict) or any(
            key not in ("sports_streams", "movies_streaming", "video_games", "social_media_reels")
            or not isinstance(value, bool)
            for key, value in data["detection_targets"].items()
        ):
            return jsonify({"success": False, "error": "Invalid detection targets"}), 400
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
    if not ip or byod_manager.validate_target(ip) is None:
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
    base_path = os.path.abspath(VIOLATIONS_DIR)
    full_path = os.path.abspath(os.path.join(base_path, filepath))
    try:
        if os.path.commonpath([base_path, full_path]) != base_path:
            return "Access denied", 403
    except ValueError:
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
