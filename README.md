# Synapse Command & AI Sentinel 🚀

Enterprise Cross-Platform Classroom Monitoring, Fleet Enforcement & BYOD Guardian System for DiGiHub.

## 🌟 Architecture Overview

1. **`sentinel/` — Synapse AI Sentinel Daemon & BYOD Watchdog**
   - Autonomous background AI monitoring loop & web console (port 5050).
   - `SynapseBYODGuard.py` & `SynapseWatchdog.py`: Persistent auto-healing Wi-Fi watchdog for student laptops. Automatically turns off Airplane Mode, re-enables wireless adapters, reconnects to `DIGIHUB` Wi-Fi, and enforces full-screen Kiosk mode upon unauthorized disconnect.
   - `byod_fleet_manager.py`: Global classroom release & reconnect controls.
   - `build_installer/`: Build pipelines for hardened standalone BYOD client installers (`Synapse-Setup-BYOD.exe`).
   - `build_installer_lab/`: Build pipelines for standard institutional lab workstation clients (`Synapse-Setup-Lab.exe`).

2. **`core-patches/` — High-Power Input Interception Engine (v4.11.7.0)**
   - Low-level Windows hooks (`WH_MOUSE_LL` & `WH_KEYBOARD_LL`) in `WindowsInputDeviceFunctions.cpp`.
   - Complete physical input freeze: blocks student hardware keyboard and mouse while keeping the desktop visible and passing through teacher remote control commands.
   - Custom low-glare dark & enterprise themes (`synapse_white_green.qss`, `custom_theme.qss`).
   - Custom sidebar and inspection panel widgets.

3. **`linux/` — Universal Linux Agent (Ubuntu, Mint, Debian, Fedora, Arch)**
   - Python-based client daemon (`synapse_linux_agent.py`) with telemetry, remote shell, active window detection, and auto-lock.
   - Hardware-level input device detachment via `xinput` and persistent systemd service.
   - Automated one-command installer: `install-agent.sh`.

4. **`deploy/` — Fleet Auto-Updater & Deployment Repository**
   - Silent auto-updater (`Check-And-AutoUpdate.ps1`) triggered every 30 minutes.
   - Manifest tracking via `client_version.json`.
   - Dual-host fallback (`\\fileserver\public\Synapse` and `\\192.168.1.50\public\Synapse`).
   - Administrative one-click tools: `Teacher-Release-BYOD.bat`, `Teacher-Enforce-BYOD.bat`, `Teacher-Uninstall-Synapse.bat`.

---
*Maintained by DiGiHub Lab Administration*
