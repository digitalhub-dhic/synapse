# Synapse Command & Fleet Management Ecosystem: Complete Technical Architecture & System Reference

---

## 1. Executive Overview & System Purpose

**Synapse** is an enterprise-grade classroom orchestration, real-time desktop monitoring, and anti-tamper fleet management platform developed for computer laboratories and Bring-Your-Own-Device (BYOD) student environments.

The platform bridges high-performance native desktop virtualization with autonomous background protection daemons to give educators 100% visibility and control over classroom devices while preventing student evasion, cheating, and distraction.

```
                      +---------------------------------------+
                      |       SYNAPSE COMMAND CENTER          |
                      |  (Teacher Console: digiagent-master)  |
                      +---------------------------------------+
                                          |
            +-----------------------------+-----------------------------+
            |                             |                             |
            v                             v                             v
+-----------------------+     +-----------------------+     +-----------------------+
|  LAB WORKSTATIONS     |     |   BYOD LAPTOPS        |     |     LINUX FLEET       |
|  - High-Security Lock |     |  - Wi-Fi Guard Kiosk  |     |  - Synapse Agent      |
|  - Hardware Freezing  |     |  - Airplane Lockout   |     |  - Token Remote CLI   |
|  - Realtime Telemetry |     |  - Virtual Snap-back  |     |  - Auto-Discovery     |
+-----------------------+     +-----------------------+     +-----------------------+
```

### Core Capabilities
1. **Real-Time Visual Monitoring**: High-frequency, multi-threaded screen capture with adaptive DPI scaling and ultra-low latency VNC streaming.
2. **High-Security Hardware Lockout**: Deep Win32 keyboard/mouse virtualization hooks that freeze physical hardware while preserving full teacher remote control.
3. **Bulletproof BYOD Wi-Fi Enforcement**: Autonomous user-session kiosks that detect unauthorized Wi-Fi disconnections, airplane mode toggles, or network switching, instantly locking student screens until classroom connectivity is restored.
4. **Zero-Config Dynamic Auto-Discovery**: Concurrent subnet sweeper and UDP beacon engine that automatically detects and registers newly installed computers without manual IP entry.
5. **Anti-Duplication Hardware Fingerprinting**: MAC-address identity resolution that seamlessly tracks dynamic DHCP IP reassignments without creating duplicate records.

---

## 2. Complete Component & Process Architecture

Synapse is structured across four distinct architectural tiers:

```
[ TIER 1: UI & ORCHESTRATION ] 
       digiagent-master.exe (Qt6 / C++)
         ├── ComputerSelectPanel (Locations & Computers Tree View with Checkboxes)
         ├── ComputerInspectorPanel (Live Telemetry & Diagnostics)
         └── VeyonMasterCore (VNC Orchestrator & Feature Controller)

[ TIER 2: LOCAL CLIENT SERVICE RUNTIME ]
       digiagent-service.exe (Windows Service / NT AUTHORITY\SYSTEM)
         ├── digiagent-server.exe (Session Management & Port 11100 Listener)
         ├── digiagent-worker.exe (Desktop Capture & Hardware Injection Worker)
         └── DigiAgentCore.dll (C++ Input Hooking & Cryptographic Validation)

[ TIER 3: AUTONOMOUS SENTINEL & BACKGROUND DAEMONS ]
       SynapseFleetDiscovery.py (Subnet Sweeper & UDP Beacon Listener)
       SynapseBYODGuard.exe (User-Session Virtual Desktop Guard & Lock Screen)
       SynapseWatchdog.exe (SYSTEM-Level Anti-Tamper & Wi-Fi Auto-Healer)
       synapse_sentinel_engine.py (AI Screen Inspection & Heuristic Detector)

[ TIER 4: FLEET DISTRIBUTION & UPDATE PIPELINE ]
       Check-And-AutoUpdate.ps1 (Atomic Staging, Rollback Safety, Network Jitter)
       \\192.168.1.50\public\Synapse\ (Centralized Read-Only Fleet Distribution Share)
```

---

### Detailed Component Breakdown

#### A. Master Console (`digiagent-master.exe`)
- **Technology**: C++20, Qt6 (QtWidgets, QtNetwork, QtConcurrent).
- **Role**: The instructor's command interface. Renders the live monitoring grid, displays individual screen thumbnails, manages remote control sessions, pushes lockouts, and executes fleet-wide classroom tools.
- **Key Modules**:
  - `ComputerSelectPanel.cpp`: Manages the "Locations & computers" hierarchy. Contains custom delegate rendering with **checkboxes (`☑` / `☐`) on every single group and workstation** so teachers can select and view any combination of groups simultaneously.
  - `CheckableItemProxyModel.cpp`: Manages check state propagation between parent locations and child computers.
  - `ComputerInspectorPanel.cpp`: Displays computer hardware specs, logged-in usernames, IP addresses, MAC addresses, and power states.

#### B. Core Agent Runtime (`DigiAgentCore.dll`, `digiagent-service.exe`, `digiagent-server.exe`)
- **Technology**: C++, Win32 API, OpenSSL, libvncserver.
- **Port**: `11100/TCP` (Management RPC & Feature Execution), `11200/TCP` (VNC Desktop Video).
- **Role**: Runs 24/7 as a Windows Service (`DigiAgentService`) under `NT AUTHORITY\SYSTEM`.
- **Deep Input Virtualization**:
  - Implements low-level keyboard (`WH_KEYBOARD_LL`) and mouse (`WH_MOUSE_LL`) Windows hooks.
  - **Zero-Trust Input Filter**: Rejects student synthetic keystrokes, AutoHotkey scripts, and software injection (`LLKHF_INJECTED`). Strictly passes only authenticated teacher remote keystrokes carrying the verified teacher signature `0x564E4300`.

#### C. BYOD Enforcement Guard (`SynapseBYODGuard.exe`)
- **Technology**: Python 3.13 / PyInstaller standalone binary, Win32 API, Tkinter.
- **Role**: Runs in the student's interactive desktop session to enforce classroom presence.
- **Behavior**:
  - Continuously validates connectivity to teacher infrastructure (`192.168.1.150:11100`, Tailscale WAN `100.99.161.43`, and mDNS `computer-50.local`).
  - If a student disconnects Wi-Fi, switches to mobile hotspot, or toggles Airplane Mode:
    1. Instantly deploys a full-screen, frameless, topmost captive lock window.
    2. Installs low-level keyboard hooks blocking `Alt+Tab`, `Alt+F4`, `Ctrl+Esc`, `LWin`, `RWin`, and Task Manager (`DisableTaskMgr`).
    3. Launches a **25ms DWM Snap-Back Watchdog** (`_snap_back_watchdog`): If the student attempts to swipe to another virtual desktop or touchpad gesture, it snaps focus back to the lock window in under 25ms.
    4. Displays a native Wi-Fi connection picker allowing the student to select the classroom Wi-Fi or enter a password.
    5. Features an emergency **Teacher Unlock** modal protected by salted SHA-256 password verification.
    6. **Zero-Lag Asynchronous Polling**: Network reachability checks run on a background worker thread (`bg_network_checker`), ensuring the GUI never freezes or shows "Not Responding".

#### D. System Watchdog (`SynapseWatchdog.exe`)
- **Technology**: Python 3.13 / PyInstaller standalone binary running as a `SYSTEM` scheduled task.
- **Role**: Anti-tamper supervisor running outside the user session.
- **Behavior**:
  - Monitors `DigiAgentService` and `SynapseBYODGuard.exe`. Auto-restarts them if terminated.
  - **Wi-Fi Auto-Healing**: If a student turns off Wi-Fi or enables Airplane Mode, the watchdog resets `SystemRadioState` registry keys, forces wireless adapters to `ENABLED`, and issues `netsh wlan connect` commands to rejoin approved school SSIDs.
  - Uses thread-level mutual exclusion (`_heal_lock`) to prevent thread fork bombs.

#### E. Dynamic Fleet Discovery Engine (`SynapseFleetDiscovery.py`)
- **Technology**: Python, multithreaded socket pool, Win32 single-instance mutex.
- **Role**: Continuously sweeps the classroom subnet (`192.168.1.1`–`254`) on port 11100 and listens for UDP broadcast self-announcements on port `11105`.
- **Anti-Duplication Logic**:
  - Extracts hardware MAC addresses from the OS ARP cache (`arp -a`).
  - Matches newly seen devices against existing records by MAC address.
  - If a device receives a new IP from DHCP, the engine updates its IP address in-place without generating a duplicate tile.
  - Lab workstation names (`computer 01`–`33`, `SUB 1`–`5`) are strictly preserved.
  - Any new/external computer is automatically registered under the **`BYOD`** group.

#### F. AI Sentinel Engine (`sentinel_daemon.py`, `synapse_sentinel_engine.py`)
- **Technology**: Flask, OpenCV (`cv2`), NumPy, PyAutoGUI.
- **Port**: `5000/TCP` (Sentinel Web Dashboard).
- **Role**: Analyzes teacher console thumbnails in real time to detect policy violations:
  - **Live Sports Streaming**: Detects field turf (`g > 45, g > r + 15, g > b + 15`) combined with continuous motion. Includes **organic texture entropy checks** (`turf_variance > 9.0`) to avoid false positives on green Excel spreadsheets or code editors.
  - **Social Media Reels / Shorts**: Detects 9:16 vertical video motion concentrated in the screen center flanked by static margins.
  - **Video Games**: Detects rapid 3D viewport shifts and high chromatic color saturation (`color_saturation > 18.0`), ignoring monochrome document scrolling.
  - Automatically issues targeted screen lock commands (`ScreenLockWithMessage`) to offending computers only.

---

## 3. Network Topology & Port Map

```
  +-------------------------------------------------------------------------+
  |                   CLASSROOM LOCAL AREA NETWORK (192.168.1.0/24)         |
  +-------------------------------------------------------------------------+
       |                                                              |
       v                                                              v
+-----------------------------+                           +-------------------------+
| TEACHER PC (192.168.1.150)  |                           | FILE SERVER             |
| - digiagent-master.exe      |                           | (192.168.1.50)          |
| - SynapseFleetDiscovery.py  |                           | - \\fileserver\public\  |
| - Ports: 11100, 11105, 5000 |                           |   Synapse\              |
+-----------------------------+                           +-------------------------+
       ^                                                              |
       | RPC / VNC Video (TCP 11100, 11200)                           | Auto-Update
       | Discovery UDP Beacon (Port 11105)                            | \\public\Synapse\
       v                                                              v
+-----------------------------------------------------------------------------------+
| STUDENT WORKSTATIONS & BYOD LAPTOPS                                               |
| - Lab PCs: 192.168.1.101 - 192.168.1.133 (computer 01 - 33)                       |
| - Sub Stations: 192.168.1.144 - 192.168.1.148 (SUB 1 - 5)                         |
| - BYOD Laptops: 192.168.1.200 (READINGHUB), 192.168.1.249 (CIVIL), etc.           |
+-----------------------------------------------------------------------------------+
```

### Complete Network Port Reference

| Port | Protocol | Component | Purpose | Security |
| :--- | :---: | :--- | :--- | :--- |
| **`11100`** | TCP | `digiagent-server.exe` | Management RPC, Feature Dispatch, Status Telemetry | RSA Public/Private Key Auth |
| **`11105`** | UDP | `SynapseFleetDiscovery` | Client Self-Announcement Broadcast Beacon | JSON payload (Hostname, IP, MAC) |
| **`11200`** | TCP | `digiagent-worker.exe` | UltraVNC Screen Streaming & Framebuffer Transfer | Encrypted Desktop Stream |
| **`11080`** | TCP | WebAPI Server | Remote REST API Management Interface | Optional HTTP / HTTPS Token Auth |
| **`11300`** | TCP | Feature Worker Manager | Inter-process feature lifecycle management | Localhost RPC |
| **`11400`** | TCP | Demo Server | Teacher Broadcast Screen Stream to Student PCs | Low-latency broadcast stream |
| **`5000`**  | TCP | `sentinel_daemon.py` | AI Sentinel Web Dashboard & Violation Audit Log | Local / LAN Web Interface |
| **`445`**   | TCP | SMB File Sharing | Auto-updater staging & installer distribution | Active Directory / Windows ACL |

---

## 4. Security, Cryptography & Anti-Tamper Hardening

```
+-------------------------------------------------------------------------+
|                  SYNAPSE ZERO-TRUST SECURITY MODEL                      |
+-------------------------------------------------------------------------+
  [1] RSA Authentication
      Master Private Key  : C:\SynapseSecureKeys\MasterPrivate\Digihub.pem
      Client Public Key   : C:\ProgramData\DigiAgent\keys\public\Digihub\key
  
  [2] Master Password Protection
      Algorithm           : PBKDF2 / Salted SHA-256 (64-character hash)
      Verification        : Constant-Time (hmac.compare_digest)
  
  [3] Input Virtualization
      Student Strokes     : Discarded (physical & synthetic SendInput)
      Teacher Remote Input: Verified via dwExtraInfo == 0x564E4300
  
  [4] Anti-Tamper Filesystem
      ACL Hardening       : Users:(OI)(CI)(RX) Deny:(DE,WDAC,WD,AD)
```

### 1. RSA Public/Private Key Pair Authentication
- Every communication between Teacher Master and Student Client is authenticated using **4096-bit RSA keys**.
- **Master Private Key**: Strictly stored on the teacher PC at `C:\SynapseSecureKeys\MasterPrivate\Digihub.pem`. (Completely removed from public network shares).
- **Public Key**: Deployed to all clients at `C:\ProgramData\DigiAgent\keys\public\Digihub\key`.
- Without the private key, no unauthorized student or device on the network can issue commands, view screens, or unlock computers.

### 2. Salted SHA-256 Master Password Hashing
- Plaintext passwords have been eliminated across all scripts (`Teacher-Uninstall-Synapse.bat`, `SynapseBYODGuard.py`, `SynapseWatchdog.py`).
- Passwords are verified using constant-time string comparison (`hmac.compare_digest`) against a salted SHA-256 digest (`PASSWORD_SALT = b"SynapseEnterpriseSalt2026"`), completely preventing timing attacks.

### 3. High-Security Block Input Mode
- When the teacher engages **Freeze Screen** or **Lock Inputs Only**:
  - Physical mouse motion and keystrokes are intercepted and dropped at the Windows hook layer.
  - Injected inputs from macros, virtual controllers, and Python scripts (`LLKHF_INJECTED`) are unconditionally dropped.
  - **Only teacher VNC injection** carrying the secret signature `p->dwExtraInfo == 0x564E4300` is allowed to pass to Windows.

### 4. Anti-Tamper Registry & ACL Policies
- `C:\Program Files\DigiAgent` and `C:\ProgramData\DigiAgent` are locked down using Win32 `icacls`:
  ```cmd
  icacls "C:\Program Files\DigiAgent" /grant "Users:(OI)(CI)(RX)" /deny "Users:(DE,WDAC,WD,AD)"
  ```
  Students cannot delete DLLs, modify configuration files, or inject rogue executables.
- Task Manager (`DisableTaskMgr = 1`) and Task View (`ShowTaskViewButton = 0`) are locked in the registry during student sessions.

---

## 5. Network Directory Organization & Sidebar Checkbox Logic

Synapse organizes fleet devices into clean logical groupings. In `digiagent-cli networkobjects list`, three primary locations exist:

```
+-- DIGIHUB (Canonical Lab Computers)
|   +-- computer 01 (192.168.1.101)
|   +-- computer 02 (192.168.1.102)
|   +-- ...
|   +-- computer 33 (192.168.1.133)
|   +-- SUB 1 - SUB 5 (192.168.1.144 - 192.168.1.147)
|   +-- computer-50 (Teacher PC: 192.168.1.150)
|
+-- BYOD (Student Laptops & Dynamic Workstations)
|   +-- READINGHUB (192.168.1.200)
|   +-- CIVIL (192.168.1.249)
|   +-- DESKTOP-HEMG7GO (192.168.1.22)
|   +-- WINDOWS (192.168.1.29)
|   +-- DESKTOP-R770MJ6 (Dynamic BYOD)
|
+-- special reserve (Isolated Workstations)
    +-- al midad (192.168.1.137)
```

### Checkbox UI Operation in Master Console
In the Master interface left sidebar (`Locations & computers`):
- Every location folder has a **Group Checkbox**.
- Every individual computer has a **Computer Checkbox**.
- **Clicking any Group Checkbox** toggles all computers within that group on or off the monitoring grid.
- **Clicking any Computer Checkbox** toggles only that specific computer.
- Multiple groups (e.g. `DIGIHUB` + `BYOD`) can be checked at the same time to display all lab PCs and student laptops together in one unified view.
- `AutoSelectCurrentLocation` is disabled (`false`), ensuring Synapse never unchecks other groups when opening.

---

## 6. Enterprise Silent Auto-Updater (`Check-And-AutoUpdate.ps1`)

The fleet auto-updater runs silently via Windows Task Scheduler across every client in the school.

### Resilient Update Lifecycle
```
[ Trigger: Scheduled Task ]
         │
         ▼
[ Step 1: Single-Instance Mutex ] ──── If updater running ────► [ Exit ]
         │
         ▼
[ Step 2: Random Jitter (1-30s) ] (Defeats server saturation)
         │
         ▼
[ Step 3: Check client_version.json ] ── If up to date ──────► [ Exit ]
         │
         ▼
[ Step 4: Atomic Staging to UpdateStage\ ]
         │  - Download DigiAgentCore.dll.tmp
         │  - Verify file size & integrity
         ▼
[ Step 5: Pre-Swap Backup ] (Creates BackupPrev\ for rollback)
         │
         ▼
[ Step 6: Atomic Swap & Service Restart ]
         │  - Copy staged files to Program Files
         │  - If error occurs ──► Restore from BackupPrev\ & Restart
         ▼
[ Step 7: Update installed_version.txt ]
```

---

## 7. Administrator Cheat Sheet & Reference Commands

### Useful Commands (Teacher Server)

```powershell
# 1. Check all network computers and their registered locations
& "C:\Program Files\DigiAgent\digiagent-cli.exe" networkobjects list

# 2. Add a new computer manually
& "C:\Program Files\DigiAgent\digiagent-cli.exe" networkobjects add computer "NAME" "IP" "MAC" "LOCATION"

# 3. Remove an object by name or UUID
& "C:\Program Files\DigiAgent\digiagent-cli.exe" networkobjects remove "NAME_OR_UUID"

# 4. Check status of running background daemons
Get-Process -Name digiagent*, python* | Format-Table Id, ProcessName, Path

# 5. Lock a specific computer remotely with a custom message
& "C:\Program Files\DigiAgent\digiagent-cli.exe" feature start 192.168.1.200 ScreenLockWithMessage '{"text":"Please pay attention to the board."}'

# 6. Unlock a specific computer
& "C:\Program Files\DigiAgent\digiagent-cli.exe" feature stop 192.168.1.200 ScreenLock
```

---

## 8. Summary of Recent Fixes Applied

1. **Checkboxes on Each and Every One**: Patched `ComputerSelectPanel.cpp` and recompiled `digiagent-master.exe` with dedicated checkbox rendering (`☑` / `☐`) on all group locations and individual computers.
2. **Separated Groups**: Moved `READINGHUB`, `CIVIL`, `DESKTOP-HEMG7GO`, and `WINDOWS` into the **`BYOD`** group. Configured `SynapseFleetDiscovery.py` to route all future dynamic computers into `BYOD`, leaving `DIGIHUB` exclusively for canonical lab stations.
3. **Hardware MAC Tracking**: Added ARP cache MAC identity resolution to eliminate duplicate entries when student devices change IPs over DHCP.
4. **Non-Blocking GUI**: Offloaded Wi-Fi reachability checks in `SynapseBYODGuard.py` to an asynchronous daemon thread, eliminating UI lag.
5. **Handle Leak Resolution**: Added explicit `CloseWindowStation` and `CloseDesktop` handle calls in interactive desktop attachment.
6. **False Positive Guard**: Added texture entropy variance filters to AI Sentinel to distinguish live sports streams from green spreadsheets and code editors.
7. **Atomic Auto-Updater**: Enhanced `Check-And-AutoUpdate.ps1` with atomic staging, rollback safety, and thundering-herd jitter protection.
