"""
Synapse AI Sentinel - Enterprise Autonomous Classroom Guardian
Monitors student screens for prohibited activities:
 - Live Sports Streams (Cricket, Football, Tennis, Hotstar Sports, Cricbuzz, etc.)
 - Movies & Entertainment Video (Netflix, Prime, YouTube full movies, piracy streams)
 - Video Games (Roblox, Minecraft, Steam, 3D browser games)
 - Select Social Media Distractions (Instagram Reels, TikTok, YouTube Shorts)

When misuse is detected:
 1. Captures and timestamps evidence screenshot to C:\\SynapseViolations\\
 2. Instantly locks ONLY the culprit computer via targeted CLI call
 3. Sounds alert & logs incident to audit database
 4. Zero modifications or corruption to DigiAgent Master or clients
"""

import os
import sys
import time
import json
import socket
import logging
import datetime
import subprocess
import threading
from PIL import Image, ImageDraw, ImageFont, ImageChops, ImageStat
import numpy as np

# Base paths
BASE_DIR = r"C:\SynapseAISentinel"
VIOLATIONS_DIR = r"C:\SynapseViolations"
CONFIG_FILE = os.path.join(BASE_DIR, "sentinel_config.json")
AUDIT_LOG_FILE = os.path.join(VIOLATIONS_DIR, "audit_log.json")
CLI_PATH = r"C:\Program Files\DigiAgent\digiagent-cli.exe"

sys.path.append(BASE_DIR)
from byod_fleet_manager import byod_manager

os.makedirs(BASE_DIR, exist_ok=True)
os.makedirs(VIOLATIONS_DIR, exist_ok=True)

# Set up logging
logging.basicConfig(
    filename=os.path.join(BASE_DIR, "sentinel.log"),
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)
console = logging.StreamHandler()
console.setLevel(logging.INFO)
logging.getLogger().addHandler(console)

DEFAULT_CONFIG = {
    "enabled": True,
    "auto_freeze": False,  # Safe default: alert & save evidence first; teacher freezes with 1 click
    "scan_interval_seconds": 2.0,
    "confirmation_cycles": 2,  # Must be seen across 2 checks to avoid false positives
    "lock_cooldown_seconds": 30,  # Don't re-lock host within 30s
    "confidence_threshold": 0.65,
    "ai_alerts_enabled": False,  # Enable only after validating local thresholds on labeled screens.
    "ai_min_similarity": 0.20,
    "ai_min_margin": 0.08,
    "detection_targets": {
        "sports_streams": True,
        "movies_streaming": True,
        "video_games": True,
        "social_media_reels": True
    },
    "sensitivity": "medium",  # "low", "medium", "strict"
    "whitelist_ips": ["192.168.137.1", "192.168.1.150", "127.0.0.1"],
    "whitelist_names": ["computer-50"],
    "play_sound": True
}

LIVE_ALERTS_FILE = os.path.join(VIOLATIONS_DIR, "live_alerts.json")


def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                cfg = json.load(f)
                return {**DEFAULT_CONFIG, **cfg}
        except Exception as e:
            logging.error(f"Error reading config: {e}")
    return DEFAULT_CONFIG.copy()


def save_config(cfg):
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
    except Exception as e:
        logging.error(f"Error saving config: {e}")


class SynapseAISentinel:
    def __init__(self):
        self.config = load_config()
        self.is_running = False
        self.computers = []
        self.data_lock = threading.Lock()
        self.previous_frames = {}       # host -> np.array (gray)
        self.violation_candidates = {}   # host -> { "count": int, "reason": str, "timestamp": float }
        self.locked_hosts = {}           # host -> { "lock_time": float, "reason": str }
        self.recent_violations = []      # list of recent events for UI
        self.last_master_crop = None
        self._ort_session = None
        self._text_embs = None
        self.load_network_catalog()
        self.load_audit_log()

    def load_network_catalog(self):
        """Loads computer directory dynamically from DigiAgent CLI."""
        try:
            cmd = [CLI_PATH, "networkobjects", "dump"]
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            comps = []
            for line in res.stdout.splitlines():
                parts = [p.strip() for p in line.split("|")]
                if len(parts) >= 6 and parts[3] == "Computer":
                    comp_name = parts[4]
                    ip = parts[5]
                    if ip:
                        comps.append({"name": comp_name, "ip": ip})
            if comps:
                self.computers = comps
                logging.info(f"Loaded {len(self.computers)} computers into Sentinel catalog.")
        except Exception as e:
            logging.error(f"Failed to query network objects: {e}")

    def load_audit_log(self):
        if os.path.exists(AUDIT_LOG_FILE):
            try:
                with open(AUDIT_LOG_FILE, "r", encoding="utf-8") as f:
                    self.recent_violations = json.load(f)[-100:]
            except Exception as e:
                logging.error(f"Failed to load audit log: {e}")

    def save_audit_entry(self, entry):
        self.recent_violations.insert(0, entry)
        if len(self.recent_violations) > 200:
            self.recent_violations.pop()
        try:
            with open(AUDIT_LOG_FILE, "w", encoding="utf-8") as f:
                json.dump(self.recent_violations, f, indent=2)
        except Exception as e:
            logging.error(f"Failed to save audit log: {e}")

    def is_whitelisted(self, name, ip):
        if ip in self.config.get("whitelist_ips", []):
            return True
        if name in self.config.get("whitelist_names", []):
            return True
        # Check if device is in RELEASED state
        if byod_manager.is_device_released(ip):
            return True
        return False

    def play_alert_sound(self):
        if not self.config.get("play_sound", True):
            return
        def _beep():
            try:
                import winsound
                # Dual urgency chime
                winsound.Beep(1046, 180)  # C6
                time.sleep(0.05)
                winsound.Beep(1318, 250)  # E6
            except Exception:
                pass
        threading.Thread(target=_beep, daemon=True).start()

    def lock_computer(self, name, ip, reason, evidence_path):
        """Locks ONLY the offending computer."""
        now = time.time()
        # Cooldown check
        with self.data_lock:
            if ip in self.locked_hosts:
                if now - self.locked_hosts[ip]["lock_time"] < self.config.get("lock_cooldown_seconds", 30):
                    return False
            self.locked_hosts[ip] = {"lock_time": now, "reason": reason}

        logging.warning(f"🚨 LOCKING CULPRIT: [{name}] ({ip}) for {reason}")

        # CLI targeted lock
        lock_msg = f"[SYNAPSE AI SENTINEL]\nPolicy Violation: {reason} Detected.\nThis computer has been locked.\nPlease contact your instructor."
        try:
            args_json = json.dumps({"text": lock_msg})
            cmd = [CLI_PATH, "feature", "start", ip, "ScreenLockWithMessage", args_json]
            subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception as e:
            logging.error(f"CLI Lock failed for {ip}: {e}")

        # Alert teacher & log
        self.play_alert_sound()
        audit_entry = {
            "timestamp": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "computer_name": name,
            "ip": ip,
            "violation_type": reason,
            "evidence_image": evidence_path,
            "status": "LOCKED"
        }
        self.save_audit_entry(audit_entry)
        return True

    def unlock_computer(self, ip):
        """Unlocks a computer."""
        try:
            cmd = [CLI_PATH, "feature", "stop", ip, "ScreenLock"]
            subprocess.run(cmd, capture_output=True, timeout=5)
            with self.data_lock:
                if ip in self.locked_hosts:
                    del self.locked_hosts[ip]
            logging.info(f"🔓 Computer {ip} unlocked successfully.")
            return True
        except Exception as e:
            logging.error(f"Failed to unlock {ip}: {e}")
            return False

    def unlock_all(self):
        """Unlocks all computers currently flagged or locked."""
        success = True
        for comp in self.computers:
            ip = comp["ip"]
            if not self.is_whitelisted(comp["name"], ip):
                try:
                    subprocess.Popen([CLI_PATH, "feature", "stop", ip, "ScreenLock"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except Exception:
                    success = False
        with self.data_lock:
            self.locked_hosts.clear()
        logging.info("🔓 Sent unlock command to all computers.")
        return success

    # ==========================================
    #          AI & HEURISTIC DETECTORS
    # ==========================================

    def analyze_frame(self, comp_name, ip, frame_rgb):
        """
        Analyzes a single student screen frame.
        Returns (violation_detected: bool, reason: str, metrics: dict)
        """
        metrics = {}
        arr = np.array(frame_rgb)
        h, w, _ = arr.shape
        if h < 30 or w < 30:
            return False, None, metrics

        gray = np.mean(arr, axis=2).astype(np.uint8)
        total_pixels = h * w

        # Check offline / static black screen
        stat = ImageStat.Stat(frame_rgb)
        mean_lum = sum(stat.mean) / 3.0
        std_lum = sum(stat.stddev) / 3.0
        if mean_lum < 35 and std_lum < 15:
            # Offline card or powered down screen
            return False, None, {"offline": True}

        # 1. Motion Calculation
        motion_ratio = 0.0
        prev_gray = self.previous_frames.get(ip)
        if prev_gray is not None and prev_gray.shape == gray.shape:
            diff = np.abs(gray.astype(int) - prev_gray.astype(int))
            # Pixels that changed significantly (> 20 intensity)
            motion_pixels = np.count_nonzero(diff > 20)
            motion_ratio = (motion_pixels / total_pixels) * 100.0

        self.previous_frames[ip] = gray
        metrics["motion_ratio"] = motion_ratio

        # Static document protection: If motion is nearly zero, ignore (coding, reading docs, writing)
        if motion_ratio < 0.8:
            return False, None, metrics

        # Sensitivity thresholds
        sens = self.config.get("sensitivity", "medium")
        sports_thresh = 16.0 if sens == "strict" else (18.0 if sens == "medium" else 22.0)
        movie_motion_thresh = 3.0 if sens == "strict" else (3.8 if sens == "medium" else 4.8)
        game_motion_thresh = 5.0 if sens == "strict" else (6.5 if sens == "medium" else 8.5)

        targets = self.config.get("detection_targets", {})
        r, g, b = arr[:, :, 0].astype(int), arr[:, :, 1].astype(int), arr[:, :, 2].astype(int)

        # --- Local-only ONNX vision model (SigLIP, CPUExecutionProvider) ---
        model_dir = os.path.join(BASE_DIR, "models", "siglip-base-patch16-224")
        quantized_model_path = os.path.join(model_dir, "image", "model-int8.onnx")
        onnx_model_path = quantized_model_path if os.path.exists(quantized_model_path) else os.path.join(model_dir, "image", "model.onnx")
        onnx_emb_path = os.path.join(model_dir, "text_embeddings.npz")
        if os.path.exists(onnx_model_path) and os.path.exists(onnx_emb_path):
            try:
                import onnxruntime as ort
                if self._ort_session is None:
                    options = ort.SessionOptions()
                    options.intra_op_num_threads = 2
                    options.inter_op_num_threads = 1
                    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
                    self._ort_session = ort.InferenceSession(
                        onnx_model_path,
                        sess_options=options,
                        providers=["CPUExecutionProvider"],
                    )
                    with np.load(onnx_emb_path, allow_pickle=False) as emb_file:
                        labels = emb_file["labels"].astype(str)
                        text_vectors = emb_file["embeddings"].astype(np.float32)
                    text_vectors /= np.maximum(np.linalg.norm(text_vectors, axis=1, keepdims=True), 1e-8)
                    self._text_embs = {"labels": labels, "embeddings": text_vectors}

                # SigLIP uses a 224x224 resize and [-1, 1] normalization.
                img_224 = frame_rgb.resize((224, 224), Image.Resampling.BICUBIC)
                inp = np.asarray(img_224, dtype=np.float32) / 127.5 - 1.0
                inp = inp.transpose(2, 0, 1)[None, ...]
                in_name = self._ort_session.get_inputs()[0].name
                out_name = self._ort_session.get_outputs()[0].name
                img_feat = self._ort_session.run([out_name], {in_name: inp})[0].astype(np.float32)
                img_feat /= np.maximum(np.linalg.norm(img_feat, axis=-1, keepdims=True), 1e-8)
                labels = self._text_embs["labels"].tolist()
                text_mat = self._text_embs["embeddings"]
                similarities = (img_feat @ text_mat.T)[0]
                order = np.argsort(similarities)
                best_idx = int(order[-1])
                best_label = str(labels[best_idx])
                best_similarity = float(similarities[best_idx])
                second_similarity = float(similarities[int(order[-2])]) if len(order) > 1 else -1.0
                margin = best_similarity - second_similarity
                metrics["ai_model"] = "siglip-base-patch16-224-int8"
                metrics["ai_label"] = best_label
                metrics["ai_similarity"] = round(best_similarity, 4)
                metrics["ai_margin"] = round(margin, 4)

                # Similarity is a ranking score, not a calibrated probability. Keep
                # model output in shadow mode until classroom examples are labeled.
                if (
                    self.config.get("ai_alerts_enabled", False)
                    and best_label != "productive"
                    and best_similarity >= self.config.get("ai_min_similarity", 0.20)
                    and margin >= self.config.get("ai_min_margin", 0.08)
                ):
                    metrics["confidence"] = max(0.0, min(1.0, best_similarity))
                    return True, f"AI review candidate: {best_label}", metrics
            except Exception as e:
                logging.debug(f"ONNX inference fallback: {e}")

        # --- A. SPORTS DETECTOR (Fullscreen & Inline/Thumbnail/PiP Detection) ---
        if targets.get("sports_streams", True):
            # Grass pitch detection: G is high, G > R + 15, G > B + 15
            turf_mask = (g > 45) & (g > r + 15) & (g > b + 15)
            # Basketball / Tennis clay court detection: Warm wood/clay (R high, G moderate, B low)
            court_mask = (r > 90) & (g > 60) & (r > g + 10) & (g > b * 1.3) & (b < 80)
            sports_mask = turf_mask | court_mask
            sports_ratio = (np.count_nonzero(sports_mask) / total_pixels) * 100.0
            metrics["sports_ratio"] = sports_ratio

            # Calculate organic texture variance in turf regions (rejects flat Excel / code editor themes)
            turf_count = np.count_nonzero(turf_mask)
            turf_variance = float(np.std(g[turf_mask])) if turf_count > 100 else 0.0
            is_organic_turf = turf_variance > 9.0

            # 1. Fullscreen / Large Window Match (requires organic texture)
            if sports_ratio > sports_thresh and motion_ratio > 2.2 and is_organic_turf:
                return True, f"Live Sports Stream ({sports_ratio:.1f}% Field Cover)", metrics

            # 2. Inline Feed Preview / Thumbnail / Picture-in-Picture / Split Screen
            # Even when not full-screen, an active video thumbnail or PiP has concentrated motion and field turf
            if prev_gray is not None and motion_ratio > 1.2 and is_organic_turf:
                # Divide screen into 4x3 localized patches to inspect micro-regions
                cols, rows = 4, 3
                pw, ph = w // cols, h // rows
                for py in range(rows):
                    for px in range(cols):
                        x1, y1 = px * pw, py * ph
                        sub_sports = sports_mask[y1:y1+ph, x1:x1+pw]
                        patch_s_ratio = (np.count_nonzero(sub_sports) / sub_sports.size) * 100.0
                        sub_diff = diff[y1:y1+ph, x1:x1+pw]
                        patch_m_ratio = (np.count_nonzero(sub_diff > 20) / sub_diff.size) * 100.0

                        # A localized video tile with turf and continuous motion
                        if patch_s_ratio > 22.0 and patch_m_ratio > 4.5:
                            return True, f"Inline / Thumbnail Sports Stream ({patch_s_ratio:.1f}% Field in sub-window)", metrics

        # --- B. DISTRACTING SOCIAL MEDIA (Vertical 9:16 Video / Reels / TikTok) ---
        if targets.get("social_media_reels", True) and motion_ratio > 2.0:
            # Check if active motion is concentrated in center 35% with quiet flanking margins
            c_start = int(w * 0.32)
            c_end = int(w * 0.68)
            diff_center = diff[:, c_start:c_end]
            diff_left = diff[:, :c_start]
            diff_right = diff[:, c_end:]

            center_motion = (np.count_nonzero(diff_center > 20) / max(1, diff_center.size)) * 100.0
            flank_pixels = diff_left.size + diff_right.size
            flank_motion = ((np.count_nonzero(diff_left > 20) + np.count_nonzero(diff_right > 20)) / max(1, flank_pixels)) * 100.0
            metrics["reel_center_motion"] = center_motion
            metrics["reel_flank_motion"] = flank_motion

            if center_motion > 4.5 and flank_motion < 1.5 and (center_motion / max(0.5, flank_motion)) > 3.0:
                return True, "Social Media Reels/Shorts (9:16 Video Feed)", metrics

        # --- C. VIDEO GAMES (High 3D Viewport Motion & Viewport Shift) ---
        if targets.get("video_games", True):
            # Chromatic contrast: games have rich color saturation compared to monochrome PDF/Word text scrolling
            color_saturation = float(np.mean(np.abs(r - g) + np.abs(g - b)))
            if motion_ratio > game_motion_thresh and std_lum > 30 and color_saturation > 18.0:
                return True, f"Video Game / 3D Gameplay ({motion_ratio:.1f}% Viewport Motion)", metrics

        # --- D. MOVIES & ENTERTAINMENT STREAMING (Letterbox + Continuous Video) ---
        if targets.get("movies_streaming", True):
            # Letterbox check: top 12% and bottom 12% are dark bands while center is active
            top_bar = gray[:int(h * 0.12), :]
            bottom_bar = gray[int(h * 0.88):, :]
            center_area = gray[int(h * 0.15):int(h * 0.85), :]

            top_mean = np.mean(top_bar)
            bot_mean = np.mean(bottom_bar)
            has_letterbox = (top_mean < 25) and (bot_mean < 25) and (np.mean(center_area) > 40)
            metrics["has_letterbox"] = has_letterbox

            # Video streaming condition: sustained motion + letterbox or high motion + saturated dynamic range
            if (has_letterbox and motion_ratio > 2.5) or (motion_ratio > movie_motion_thresh and std_lum > 35):
                return True, f"Movie / Entertainment Video Stream ({motion_ratio:.1f}% Motion)", metrics

            # Inline / Windowed Video Stream (Patch-level)
            if prev_gray is not None and motion_ratio > 1.5:
                cols, rows = 4, 3
                pw, ph = w // cols, h // rows
                for py in range(rows):
                    for px in range(cols):
                        sub_diff = diff[py*ph:(py+1)*ph, px*pw:(px+1)*pw]
                        patch_m = (np.count_nonzero(sub_diff > 20) / sub_diff.size) * 100.0
                        if patch_m > 16.0:
                            return True, f"Inline Video / Movie Stream ({patch_m:.1f}% Motion in sub-window)", metrics

        return False, None, metrics

    def save_evidence_image(self, comp_name, ip, reason, screen_crop, full_master_img=None):
        """Generates a professional evidence image with watermark header."""
        today = datetime.datetime.now().strftime("%Y-%m-%d")
        daily_dir = os.path.join(VIOLATIONS_DIR, today)
        os.makedirs(daily_dir, exist_ok=True)

        timestamp = datetime.datetime.now().strftime("%H%M%S")
        clean_name = comp_name.replace(" ", "_").replace("\\", "_")
        filename = f"VIOLATION_{timestamp}_{clean_name}_{ip.replace('.', '-')}.png"
        filepath = os.path.join(daily_dir, filename)

        # Build evidence banner
        w, h = screen_crop.size
        banner_h = 44
        canvas = Image.new("RGB", (w, h + banner_h), (17, 26, 45))
        canvas.paste(screen_crop, (0, banner_h))

        draw = ImageDraw.Draw(canvas)
        # Red warning bar
        draw.rectangle([(0, 0), (w, 4)], fill=(239, 68, 68))
        # Banner text
        title_text = f"SYNAPSE AI EVIDENCE: {comp_name} ({ip})"
        reason_text = f"VIOLATION: {reason} | {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
        draw.text((10, 8), title_text, fill=(255, 255, 255))
        draw.text((10, 24), reason_text, fill=(248, 113, 113))

        canvas.save(filepath)
        logging.info(f"📸 Evidence saved to: {filepath}")
        return filepath

    # ==========================================
    #          SENTINEL SCAN LOOP
    # ==========================================

    def publish_live_alert(self, name, ip, reason, evidence_path, confidence=0.75, status="ALERT"):
        """Writes live alert for Synapse Command bellBtn 1-click teacher review."""
        entry = {
            "timestamp": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "computer_name": name,
            "ip": ip,
            "violation_type": reason,
            "confidence": confidence,
            "evidence_image": evidence_path,
            "status": status
        }
        self.save_audit_entry(entry)
        self.play_alert_sound()
        try:
            alerts = []
            if os.path.exists(LIVE_ALERTS_FILE):
                with open(LIVE_ALERTS_FILE, "r", encoding="utf-8") as f:
                    alerts = json.load(f)
            alerts = [a for a in alerts if a.get("ip") != ip]
            alerts.insert(0, entry)
            with open(LIVE_ALERTS_FILE, "w", encoding="utf-8") as f:
                json.dump(alerts[:25], f, indent=2)
        except Exception as e:
            logging.error(f"Failed to write live alerts: {e}")

    def scan_direct_frames(self, frames_list):
        """Processes direct per-computer framebuffers from veyon-master (no 7x5 crop)."""
        import base64
        from io import BytesIO
        if not self.config.get("enabled", True):
            return

        for item in frames_list:
            comp_name = item.get("name") or item.get("ip", "Unknown")
            ip = item.get("ip", "")
            b64 = item.get("frame_b64", "")
            if not ip or not b64 or self.is_whitelisted(comp_name, ip):
                continue

            try:
                raw = base64.b64decode(b64)
                frame_img = Image.open(BytesIO(raw)).convert("RGB")
            except Exception:
                continue

            is_violation, reason, metrics = self.analyze_frame(comp_name, ip, frame_img)
            if is_violation:
                cand = self.violation_candidates.get(ip, {"count": 0, "reason": reason})
                cand["count"] += 1
                cand["reason"] = reason
                self.violation_candidates[ip] = cand

                required_cycles = self.config.get("confirmation_cycles", 2)
                if cand["count"] >= required_cycles:
                    evidence_path = self.save_evidence_image(comp_name, ip, reason, frame_img)
                    conf = metrics.get("confidence", 0.78)
                    if self.config.get("auto_freeze", False):
                        self.lock_computer(comp_name, ip, reason, evidence_path)
                        self.publish_live_alert(comp_name, ip, reason, evidence_path, conf, "LOCKED")
                    else:
                        logging.warning(f"🔔 AI ALERT (Review): [{comp_name}] ({ip}) -> {reason}")
                        self.publish_live_alert(comp_name, ip, reason, evidence_path, conf, "ALERT")
                    del self.violation_candidates[ip]
            else:
                if ip in self.violation_candidates:
                    del self.violation_candidates[ip]

    def scan_cycle(self, master_screenshot):
        """Processes one frame of the Synapse Command window grid."""
        if not self.config.get("enabled", True):
            return

        # Grid bounds in standard 1920x1080 Synapse Command window
        # Left edge: ~8 to ~1472, Top: ~175 to ~925
        col_w = (1472 - 8) / 7.0
        row_h = (925 - 175) / 5.0

        num_comps = len(self.computers)
        for idx in range(num_comps):
            comp = self.computers[idx]
            comp_name = comp["name"]
            ip = comp["ip"]

            if self.is_whitelisted(comp_name, ip):
                continue

            r = idx // 7
            c = idx % 7
            if r >= 5:
                break

            x1 = int(8 + c * col_w)
            y1 = int(175 + r * row_h)
            x2 = int(x1 + col_w)
            y2 = int(y1 + row_h)

            # Crop screen preview (margin 6px, leave 56px at bottom)
            screen_crop = master_screenshot.crop((x1 + 6, y1 + 6, x2 - 6, y2 - 56))

            # Run detection
            is_violation, reason, metrics = self.analyze_frame(comp_name, ip, screen_crop)

            if is_violation:
                # Multi-frame debounce to prevent single-frame false positives
                cand = self.violation_candidates.get(ip, {"count": 0, "reason": reason})
                cand["count"] += 1
                cand["reason"] = reason
                self.violation_candidates[ip] = cand

                required_cycles = self.config.get("confirmation_cycles", 2)
                logging.info(f"⚠️ Candidate violation on {comp_name} ({ip}): {reason} [{cand['count']}/{required_cycles}]")

                if cand["count"] >= required_cycles:
                    # CONFIRMED VIOLATION
                    evidence_path = self.save_evidence_image(comp_name, ip, reason, screen_crop, master_screenshot)
                    self.lock_computer(comp_name, ip, reason, evidence_path)
                    del self.violation_candidates[ip]
            else:
                # Reset candidate count on clean frame
                if ip in self.violation_candidates:
                    del self.violation_candidates[ip]


# Singleton instance
sentinel_instance = SynapseAISentinel()
