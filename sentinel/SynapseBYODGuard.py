"""
Synapse Hardened BYOD Wi-Fi Guard - Enterprise Bulletproof Edition
"""

import os
import sys
import time
import socket
import subprocess
import threading
import tempfile
import ctypes
from ctypes import wintypes
import tkinter as tk
from tkinter import ttk

MASTER_PASS = "Synapse#Master2026!"
LOG_FILE = r"C:\ProgramData\DigiAgent\guard.log"
CONFIG_FILE = r"C:\ProgramData\DigiAgent\byod_policy.json"
CLASSROOM_SERVER = "192.168.1.150"
TAILSCALE_SERVER = "100.99.161.43"

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

# Single-Instance Enforcement: Prevents duplicate guard processes
_MUTEX_NAME = "Global\\SynapseBYODGuard_SingleInstance_Mutex"
_mutex_handle = kernel32.CreateMutexW(None, False, _MUTEX_NAME)
if kernel32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
    sys.exit(0)

def log(msg):
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    entry = f"[{ts}] {msg}"
    print(entry, flush=True)
    try:
        os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(entry + "\n")
    except Exception:
        pass

# Low-level hook constants & structures
WH_KEYBOARD_LL = 13
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_SYSKEYDOWN = 0x0104
WM_SYSKEYUP = 0x0105

VK_TAB = 0x09
VK_ESCAPE = 0x1B
VK_SPACE = 0x20
VK_F4 = 0x73
VK_LWIN = 0x5B
VK_RWIN = 0x5C

class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ('vkCode', wintypes.DWORD),
        ('scanCode', wintypes.DWORD),
        ('flags', wintypes.DWORD),
        ('time', wintypes.DWORD),
        ('dwExtraInfo', ctypes.c_size_t),
    ]

HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM)

# Screen metrics
SM_XVIRTUALSCREEN = 76
SM_YVIRTUALSCREEN = 77
SM_CXVIRTUALSCREEN = 78
SM_CYVIRTUALSCREEN = 79

HWND_TOPMOST = -1
SWP_SHOWWINDOW = 0x0040
SW_HIDE = 0
SW_SHOW = 5

def attach_to_interactive_desktop():
    """Attaches process and thread to WinSta0\\Default to guarantee interactive GUI."""
    try:
        hwinsta = user32.OpenWindowStationW("WinSta0", False, 0x037F)
        if hwinsta:
            user32.SetProcessWindowStation(hwinsta)
        hdesk = user32.OpenDesktopW("Default", 0, False, 0x01FF)
        if hdesk:
            user32.SetThreadDesktop(hdesk)
    except Exception as e:
        log(f"attach_to_interactive_desktop error: {e}")

def is_wifi_interface_connected():
    """Checks if any Wi-Fi interface is actively in 'connected' state."""
    try:
        proc = subprocess.run(
            ["netsh", "wlan", "show", "interfaces"],
            capture_output=True,
            text=True,
            timeout=2,
            creationflags=0x08000000
        )
        for line in proc.stdout.splitlines():
            line = line.strip().lower()
            if line.startswith("state") and "connected" in line:
                return True
    except Exception:
        pass
    return False

def can_reach_master():
    """Tests TCP connection to Classroom LAN (192.168.1.150) or Tailscale WAN (100.99.161.43)."""
    targets = [
        (CLASSROOM_SERVER, 11100),
        (TAILSCALE_SERVER, 11100),
    ]
    for host, port in targets:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.6)
            s.connect((host, port))
            s.close()
            return True
        except Exception:
            pass
    return False

def is_device_online():
    """
    Device is online if it can reach the Master (LAN or Tailscale).
    If Wi-Fi is physically disconnected and cannot reach Master, device is offline.
    """
    if can_reach_master():
        return True
    # If cannot reach master, double-check Wi-Fi
    if not is_wifi_interface_connected():
        return False
    # Wi-Fi says connected, but cannot reach master
    return False

def is_teacher_released():
    """Checks if the teacher put the computer in released state via byod_policy.json."""
    if os.path.exists(CONFIG_FILE):
        try:
            import json
            with open(CONFIG_FILE, "r") as f:
                data = json.load(f)
                return bool(data.get("released", False))
        except Exception:
            pass
    return False

def set_system_policies(locked: bool):
    """Enables or disables anti-bypass policies during lockout."""
    val = "1" if locked else "0"
    try:
        # Disable Task Manager
        subprocess.run(
            ["reg", "add", r"HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\System",
             "/v", "DisableTaskMgr", "/t", "REG_DWORD", "/d", val, "/f"],
            capture_output=True, creationflags=0x08000000
        )
        # Hide Task View button
        subprocess.run(
            ["reg", "add", r"HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced",
             "/v", "ShowTaskViewButton", "/t", "REG_DWORD", "/d", "0" if locked else "1", "/f"],
            capture_output=True, creationflags=0x08000000
        )
        # Disable Timeline / Activity Feed
        subprocess.run(
            ["reg", "add", r"HKLM\SOFTWARE\Policies\Microsoft\Windows\System",
             "/v", "EnableActivityFeed", "/t", "REG_DWORD", "/d", "0", "/f"],
            capture_output=True, creationflags=0x08000000
        )
    except Exception as e:
        log(f"set_system_policies error: {e}")

def ensure_tailscale_service():
    """Auto-recovers Tailscale if stopped."""
    try:
        proc = subprocess.run(
            ["sc", "query", "Tailscale"],
            capture_output=True,
            text=True,
            timeout=2,
            creationflags=0x08000000
        )
        if "STOPPED" in proc.stdout or "STOP_PENDING" in proc.stdout:
            log("Tailscale service was stopped. Restarting...")
            subprocess.run(["sc", "start", "Tailscale"], capture_output=True, creationflags=0x08000000)
    except Exception:
        pass

def get_scanned_wifi_networks():
    """Returns list of visible SSIDs and saved profiles."""
    ssids = []
    try:
        proc = subprocess.run(
            ["netsh", "wlan", "show", "networks"],
            capture_output=True, text=True, timeout=3, creationflags=0x08000000
        )
        for line in proc.stdout.splitlines():
            line = line.strip()
            if line.startswith("SSID") and ":" in line:
                ssid = line.split(":", 1)[1].strip()
                if ssid and ssid not in ssids:
                    ssids.append(ssid)
    except Exception:
        pass

    try:
        proc2 = subprocess.run(
            ["netsh", "wlan", "show", "profiles"],
            capture_output=True, text=True, timeout=2, creationflags=0x08000000
        )
        for line in proc2.stdout.splitlines():
            if "All User Profile" in line and ":" in line:
                p = line.split(":", 1)[1].strip()
                if p and p not in ssids:
                    ssids.append(p)
    except Exception:
        pass

    return ssids

def connect_to_wifi(ssid, password=""):
    """Connects to a Wi-Fi network with or without password."""
    if not ssid:
        return False, "Please select a Wi-Fi network."

    try:
        if password:
            xml = (
                '<?xml version="1.0"?>\n'
                '<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">\n'
                f'    <name>{ssid}</name>\n'
                '    <SSIDConfig>\n'
                '        <SSID>\n'
                f'            <name>{ssid}</name>\n'
                '        </SSID>\n'
                '    </SSIDConfig>\n'
                '    <connectionType>ESS</connectionType>\n'
                '    <connectionMode>auto</connectionMode>\n'
                '    <MSM>\n'
                '        <security>\n'
                '            <authEncryption>\n'
                '                <authentication>WPA2PSK</authentication>\n'
                '                <encryption>AES</encryption>\n'
                '                <useOneX>false</useOneX>\n'
                '            </authEncryption>\n'
                '            <sharedKey>\n'
                '                <keyType>passPhrase</keyType>\n'
                '                <protected>false</protected>\n'
                f'                <keyMaterial>{password}</keyMaterial>\n'
                '            </sharedKey>\n'
                '        </security>\n'
                '    </MSM>\n'
                '</WLANProfile>\n'
            )
            tmp_path = os.path.join(tempfile.gettempdir(), f"wlan_{int(time.time())}.xml")
            with open(tmp_path, "w", encoding="utf-8") as f:
                f.write(xml)

            subprocess.run(
                ["netsh", "wlan", "add", "profile", f"filename={tmp_path}"],
                capture_output=True, creationflags=0x08000000
            )
            try:
                os.remove(tmp_path)
            except Exception:
                pass

        res = subprocess.run(
            ["netsh", "wlan", "connect", f"name={ssid}"],
            capture_output=True, text=True, timeout=5, creationflags=0x08000000
        )
        return True, f"Connecting to {ssid}..."
    except Exception as e:
        return False, str(e)


class BulletproofLockScreen:
    """Enterprise Lockdown Controller."""
    def __init__(self):
        self.active = False
        self.emergency_unlocked = False
        self.root = None
        self.top_hwnd = None
        self.hook = None
        self.hook_proc_ref = None
        self.watchdog_thread = None
        self.stop_watchdog = False

    def _hook_callback(self, nCode, wParam, lParam):
        if nCode >= 0:
            kb = KBDLLHOOKSTRUCT.from_address(lParam)
            vk = kb.vkCode

            # Block Windows keys completely (LWIN & RWIN)
            if vk in (VK_LWIN, VK_RWIN):
                return 1

            # Block Alt+Tab
            if vk == VK_TAB and (kb.flags & 0x20):
                return 1

            # Block Alt+Esc
            if vk == VK_ESCAPE and (kb.flags & 0x20):
                return 1

            # Block Alt+F4
            if vk == VK_F4 and (kb.flags & 0x20):
                return 1

            # Block Ctrl+Esc
            if vk == VK_ESCAPE and (user32.GetKeyState(0x11) < 0): # VK_CONTROL
                return 1

            # Block any key combination while Windows key is held
            if (user32.GetKeyState(VK_LWIN) < 0) or (user32.GetKeyState(VK_RWIN) < 0):
                return 1

        return user32.CallNextHookEx(None, nCode, wParam, lParam)

    def _install_hook(self):
        try:
            self.hook_proc_ref = HOOKPROC(self._hook_callback)
            self.hook = user32.SetWindowsHookExW(WH_KEYBOARD_LL, self.hook_proc_ref, 0, 0)
            log(f"Low-level keyboard hook installed: handle={self.hook}")
        except Exception as e:
            log(f"Failed to install keyboard hook: {e}")

    def _uninstall_hook(self):
        if self.hook:
            try:
                user32.UnhookWindowsHookEx(self.hook)
                log("Low-level keyboard hook uninstalled.")
            except Exception as e:
                log(f"Error unhooking: {e}")
            self.hook = None
            self.hook_proc_ref = None

    def _hide_taskbar(self, hide: bool):
        try:
            cmd = SW_HIDE if hide else SW_SHOW
            h_tb = user32.FindWindowW("Shell_TrayWnd", None)
            if h_tb:
                user32.ShowWindow(h_tb, cmd)
            h_sec = user32.FindWindowW("Shell_SecondaryTrayWnd", None)
            if h_sec:
                user32.ShowWindow(h_sec, cmd)
        except Exception as e:
            log(f"Error toggling taskbar: {e}")

    def _snap_back_watchdog(self, vx, vy, vw, vh):
        """High-frequency thread defeating virtual desktop switches & touchpad swipes."""
        log("Snap-back watchdog started.")
        while not self.stop_watchdog and self.active:
            try:
                if self.top_hwnd and user32.IsWindow(self.top_hwnd):
                    fg = user32.GetForegroundWindow()
                    if fg != self.top_hwnd:
                        # DWM Snap back to lock window's virtual desktop
                        user32.SwitchToThisWindow(self.top_hwnd, True)
                        user32.SetForegroundWindow(self.top_hwnd)
                        user32.BringWindowToTop(self.top_hwnd)
                        user32.SetWindowPos(self.top_hwnd, HWND_TOPMOST, vx, vy, vw, vh, SWP_SHOWWINDOW)
                time.sleep(0.025) # 25ms check
            except Exception:
                time.sleep(0.05)
        log("Snap-back watchdog terminated.")

    def lock(self):
        if self.active or self.emergency_unlocked or is_teacher_released():
            return

        log("LOCK ACTIVATING: Classroom network disconnected!")
        self.active = True
        set_system_policies(True)
        self._hide_taskbar(True)
        self._install_hook()

        # Multi-monitor virtual bounds
        vx = user32.GetSystemMetrics(SM_XVIRTUALSCREEN)
        vy = user32.GetSystemMetrics(SM_YVIRTUALSCREEN)
        vw = user32.GetSystemMetrics(SM_CXVIRTUALSCREEN)
        vh = user32.GetSystemMetrics(SM_CYVIRTUALSCREEN)
        if vw <= 0 or vh <= 0:
            vw = user32.GetSystemMetrics(0) # SM_CXSCREEN
            vh = user32.GetSystemMetrics(1) # SM_CYSCREEN
            vx, vy = 0, 0

        log(f"Lock screen geometry: {vw}x{vh}+{vx}+{vy}")

        try:
            self.root = tk.Tk()
            self.root.title("Synapse Classroom Lock")
            self.root.geometry(f"{vw}x{vh}+{vx}+{vy}")
            self.root.overrideredirect(True)
            self.root.attributes("-topmost", True)
            self.root.configure(bg="#0B132B")

            self.root.update_idletasks()
            wid = self.root.winfo_id()
            parent = user32.GetParent(wid)
            self.top_hwnd = parent if parent else wid

            # Start snap-back watchdog
            self.stop_watchdog = False
            self.watchdog_thread = threading.Thread(
                target=self._snap_back_watchdog,
                args=(vx, vy, vw, vh),
                daemon=True
            )
            self.watchdog_thread.start()

            # Prevent Alt+F4 / Escape
            self.root.protocol("WM_DELETE_WINDOW", lambda: None)
            self.root.bind("<Alt-F4>", lambda e: "break")
            self.root.bind("<Escape>", lambda e: "break")

            # UI Layout
            card = tk.Frame(self.root, bg="#1C2541", padx=45, pady=32, relief=tk.RAISED, bd=2)
            card.place(relx=0.5, rely=0.5, anchor=tk.CENTER)

            lbl_icon = tk.Label(card, text="⚠️", font=("Segoe UI", 48), fg="#FF5964", bg="#1C2541")
            lbl_icon.pack(pady=(0, 4))

            lbl_title = tk.Label(card, text="Classroom Network Disconnected", font=("Segoe UI", 22, "bold"), fg="#FFFFFF", bg="#1C2541")
            lbl_title.pack(pady=(0, 8))

            lbl_desc = tk.Label(
                card,
                text="An active network connection to the classroom is required during class.\n"
                     "Select School Wi-Fi or your Mobile Hotspot to unlock.",
                font=("Segoe UI", 11),
                fg="#CBD5E1",
                bg="#1C2541",
                justify=tk.CENTER
            )
            lbl_desc.pack(pady=(0, 16))

            # Wi-Fi Section
            frame_wifi = tk.Frame(card, bg="#1C2541")
            frame_wifi.pack(fill=tk.X, pady=(0, 10))

            lbl_select = tk.Label(frame_wifi, text="Available Wi-Fi / Hotspot Networks:", font=("Segoe UI", 10, "bold"), fg="#94A3B8", bg="#1C2541")
            lbl_select.pack(anchor=tk.W, pady=(0, 4))

            combo_ssid = ttk.Combobox(frame_wifi, font=("Segoe UI", 11), state="readonly")
            combo_ssid.pack(fill=tk.X, pady=(0, 8))

            lbl_pwd_hint = tk.Label(frame_wifi, text="Wi-Fi Password (leave empty if saved profile):", font=("Segoe UI", 10), fg="#94A3B8", bg="#1C2541")
            lbl_pwd_hint.pack(anchor=tk.W, pady=(0, 4))

            entry_wifi_pwd = tk.Entry(frame_wifi, show="*", font=("Segoe UI", 11), bg="#0B132B", fg="#FFFFFF", insertbackground="#FFFFFF")
            entry_wifi_pwd.pack(fill=tk.X, pady=(0, 8))

            lbl_status = tk.Label(frame_wifi, text="", font=("Segoe UI", 10, "bold"), fg="#38BDF8", bg="#1C2541")
            lbl_status.pack(pady=(0, 8))

            def refresh_networks():
                if not self.active or not self.root:
                    return
                try:
                    nets = get_scanned_wifi_networks()
                    current = combo_ssid.get()
                    combo_ssid["values"] = nets
                    if nets and (not current or current not in nets):
                        combo_ssid.current(0)
                except Exception as e:
                    log(f"refresh_networks error: {e}")
                if self.active and self.root:
                    self.root.after(3500, refresh_networks)

            def do_connect():
                ssid = combo_ssid.get().strip()
                pwd = entry_wifi_pwd.get()
                if not ssid:
                    lbl_status.config(text="Please choose a Wi-Fi network from the list.", fg="#EF4444")
                    return
                lbl_status.config(text=f"Connecting to {ssid}...", fg="#38BDF8")
                self.root.update()

                success, msg = connect_to_wifi(ssid, pwd)
                if success:
                    lbl_status.config(text=msg, fg="#10B981")
                else:
                    lbl_status.config(text=msg, fg="#EF4444")

            btn_connect = tk.Button(
                frame_wifi,
                text="📶  Connect to Wi-Fi",
                font=("Segoe UI", 12, "bold"),
                bg="#3A86FF",
                fg="white",
                activebackground="#2563EB",
                activeforeground="white",
                padx=20,
                pady=7,
                relief=tk.FLAT,
                cursor="hand2",
                command=do_connect
            )
            btn_connect.pack(fill=tk.X)

            # Teacher Unlock Section
            frame_teacher = tk.Frame(card, bg="#1C2541")
            frame_teacher.pack(pady=(12, 0), fill=tk.X)

            btn_unlock_toggle = tk.Button(
                frame_teacher,
                text="🔑  Teacher Unlock",
                font=("Segoe UI", 9),
                bg="#2A3554",
                fg="#94A3B8",
                activebackground="#3B4970",
                activeforeground="white",
                padx=12,
                pady=4,
                relief=tk.FLAT,
                cursor="hand2"
            )
            btn_unlock_toggle.pack()

            frame_teacher_pwd = tk.Frame(frame_teacher, bg="#1C2541")
            lbl_tpwd = tk.Label(frame_teacher_pwd, text="Enter Teacher Master Password:", font=("Segoe UI", 10, "bold"), fg="#CBD5E1", bg="#1C2541")
            lbl_tpwd.pack(pady=(4, 4))

            entry_tpwd = tk.Entry(frame_teacher_pwd, show="*", font=("Segoe UI", 11), width=24, justify=tk.CENTER, bg="#0B132B", fg="#FFFFFF", insertbackground="#FFFFFF")
            entry_tpwd.pack(pady=(0, 6))

            lbl_tpwd_err = tk.Label(frame_teacher_pwd, text="", font=("Segoe UI", 9), fg="#EF4444", bg="#1C2541")
            lbl_tpwd_err.pack(pady=(0, 4))

            row_tbtns = tk.Frame(frame_teacher_pwd, bg="#1C2541")
            row_tbtns.pack()

            def submit_teacher_pwd(event=None):
                if entry_tpwd.get() == MASTER_PASS:
                    log("Teacher Master Password entered. Unlocking session...")
                    self.emergency_unlocked = True
                    self.unlock()
                else:
                    lbl_tpwd_err.config(text="Incorrect password. Please try again.")
                    entry_tpwd.delete(0, tk.END)

            def show_teacher_box():
                btn_unlock_toggle.pack_forget()
                lbl_tpwd_err.config(text="")
                entry_tpwd.delete(0, tk.END)
                frame_teacher_pwd.pack()
                entry_tpwd.focus_set()

            def cancel_teacher_box():
                frame_teacher_pwd.pack_forget()
                btn_unlock_toggle.pack()

            btn_tsubmit = tk.Button(row_tbtns, text="Unlock", font=("Segoe UI", 9, "bold"), bg="#10B981", fg="white", activebackground="#059669", padx=12, pady=3, relief=tk.FLAT, cursor="hand2", command=submit_teacher_pwd)
            btn_tsubmit.pack(side=tk.LEFT, padx=5)

            btn_tcancel = tk.Button(row_tbtns, text="Cancel", font=("Segoe UI", 9), bg="#475569", fg="white", activebackground="#334155", padx=12, pady=3, relief=tk.FLAT, cursor="hand2", command=cancel_teacher_box)
            btn_tcancel.pack(side=tk.LEFT, padx=5)

            entry_tpwd.bind("<Return>", submit_teacher_pwd)
            btn_unlock_toggle.config(command=show_teacher_box)

            # Polling for auto-reconnect
            def poll_status():
                if not self.active:
                    return
                if is_device_online() or is_teacher_released():
                    log("Reconnected to Classroom! Unlocking desktop...")
                    lbl_status.config(text="✅ Reconnected to Classroom! Restoring desktop...", fg="#10B981")
                    self.root.update()
                    time.sleep(0.4)
                    self.unlock()
                    return
                if self.active and self.root:
                    self.root.after(800, poll_status)

            refresh_networks()
            poll_status()
            self.root.mainloop()

        except Exception as e:
            log(f"Exception inside lock GUI: {e}")
            self.unlock()

    def unlock(self):
        log("UNLOCK EXECUTING: Restoring desktop and controls.")
        self.active = False
        self.stop_watchdog = True

        self._uninstall_hook()
        self._hide_taskbar(False)
        set_system_policies(False)

        if self.root:
            try:
                self.root.destroy()
            except Exception:
                pass
            self.root = None

        self.top_hwnd = None
        log("Desktop unlock completed successfully.")


guard_lock = BulletproofLockScreen()

def main():
    log("==========================================")
    log("SynapseBYODGuard Enterprise starting up...")
    log(f"PID: {os.getpid()}, User: {os.environ.get('USERNAME', 'Unknown')}")

    # Attach to interactive desktop
    attach_to_interactive_desktop()

    # Single instance mutex
    ERROR_ALREADY_EXISTS = 183
    mutex = kernel32.CreateMutexW(None, True, "SynapseBYODGuard_SingleInstance_Mutex")
    if kernel32.GetLastError() == ERROR_ALREADY_EXISTS:
        log("Another instance of SynapseBYODGuard is already running. Exiting.")
        sys.exit(0)

    time.sleep(1)
    disconn_count = 0

    while True:
        try:
            ensure_tailscale_service()

            if is_teacher_released():
                if guard_lock.active:
                    guard_lock.unlock()
                time.sleep(3)
                continue

            online = is_device_online()
            if not online:
                disconn_count += 1
                log(f"Network check: OFFLINE (counter={disconn_count})")
                # Trigger lock on 1st offline confirmation (~1s)
                if disconn_count >= 1 and not guard_lock.active:
                    guard_lock.lock()
            else:
                if disconn_count > 0:
                    log("Network check: RESTORED ONLINE")
                disconn_count = 0
                guard_lock.emergency_unlocked = False
                if guard_lock.active:
                    guard_lock.unlock()

            time.sleep(0.8)
        except Exception as e:
            log(f"Exception in main_loop: {e}")
            time.sleep(1.0)

if __name__ == "__main__":
    main()
