# Synapse Command & AI Sentinel

Classroom monitoring and control software for DiGiHub lab devices and enrolled BYOD laptops.

**Current readiness: not yet verified for production BYOD deployment.** The repository has working lab controls, but the BYOD release/re-enforcement path has no client acknowledgement, Linux Wayland capture has not been validated on supported hardware, and the Windows updater remains fail-closed until its publisher certificate thumbprint is configured. Python dependency ranges are now documented, but they are not hash-locked. Installer payload binaries and the Tailscale MSI remain external build inputs; record and verify their versions, signatures, and hashes before a release.

## 🌟 Architecture Overview

1. **`sentinel/` — Synapse AI Sentinel Daemon & BYOD Watchdog**
   - Local monitoring daemon and teacher web console (port 5050).
   - The teacher console requires a per-Windows-user password provisioned by `sentinel/Set-SentinelConsolePassword.ps1`; unauthenticated dashboard/API requests are rejected.
   - `SynapseBYODGuard.py` & `SynapseWatchdog.py`: Windows BYOD connection guard and lock UI. Their behavior depends on Windows session, permissions, and network configuration.
   - `byod_fleet_manager.py`: Sends release/re-enforcement requests; the current protocol does not confirm applied client state.
   - `build_installer/`: Windows BYOD packaging sources. The deployment installer requires a fresh visible consent record.
   - `build_installer_lab/`: Build pipelines for standard institutional lab workstation clients (`Synapse-Setup-Lab.exe`).
   - `requirements-console.txt` and `build_installer/requirements-build.txt`: Python dependency ranges for the Sentinel dashboard and Windows package build. They are not hash-locked; production release artifacts still need a recorded dependency and vendor-payload bill of materials.

2. **`core-patches/` — High-Power Input Interception Engine (v4.11.7.0)**
   - Low-level Windows hooks (`WH_MOUSE_LL` & `WH_KEYBOARD_LL`) in `WindowsInputDeviceFunctions.cpp`.
   - Complete physical input freeze: blocks student hardware keyboard and mouse while keeping the desktop visible and passing through teacher remote control commands.
   - Custom low-glare dark & enterprise themes (`synapse_white_green.qss`, `custom_theme.qss`).
   - Custom sidebar and inspection panel widgets.

3. **`linux/` — Linux migration and readiness helpers**
   - The former custom root daemon and unauthenticated x11vnc implementation were removed.
   - Use the Veyon-based BYOD or Lab installer under `deploy/2-Linux/`. Wayland requires Veyon 4.11.5+ and a working desktop portal; the target hardware/session has not been validated yet.
   - Linux BYOD enrollment requires an interactive `YES` consent and records the accepting account/time after readiness checks pass.
   - `install-agent.sh` is a compatibility wrapper that retires old service installs and delegates to the supported BYOD installer.

4. **`deploy/` — Fleet Auto-Updater & Deployment Repository**
   - Windows auto-updater (`Check-And-AutoUpdate.ps1`) is scheduled every 30 minutes and requires a locally pinned Authenticode signer before accepting payloads.
   - Linux clients are installed and updated through the distribution package path; the Windows updater does not update Linux.
   - Manifest tracking via `client_version.json`.
   - Dual-host fallback (`\\fileserver\public\Synapse` and `\\192.168.1.50\public\Synapse`).
   - Administrative one-click tools: `Teacher-Release-BYOD.bat`, `Teacher-Enforce-BYOD.bat`, `Teacher-Uninstall-Synapse.bat`.

On the teacher Windows account, the first launch of `sentinel/start_sentinel.bat` asks for a console password and stores a salted verifier plus cookie-signing key in that account's protected Local AppData. Copy `sentinel_auth.py`, `Set-SentinelConsolePassword.ps1`, the templates, and the daemon together into `C:\SynapseAISentinel`.

---
*Maintained by DiGiHub Lab Administration*
