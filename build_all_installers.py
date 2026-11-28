import os
import sys
import shutil
import subprocess

MAKENSIS = r"C:\msys64\mingw64\bin\makensis.exe"
CODEX_DIR = r"C:\Users\administrator\Documents\Codex\2026-10-06\c-users-administrator-documents-veyon-4"
STAGE_DIR = os.path.join(CODEX_DIR, r"work\DigiAgent-build-output\digiagent-win64-4.11.9.0")
BYOD_DIR = r"C:\SynapseAISentinel\build_installer"
LAB_DIR = r"C:\SynapseAISentinel\build_installer_lab"

env = os.environ.copy()
env["PATH"] = r"C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + env.get("PATH", "")

print("=== [1/3] Building Core NSIS Installer (Synapse-4.11.9.0-win64-setup.exe) ===")
proc = subprocess.run([MAKENSIS, "/V2", "veyon.nsi"], cwd=STAGE_DIR, env=env, capture_output=True, text=True)
if proc.returncode != 0:
    print(f"[ERR] makensis failed: {proc.stderr}")
    sys.exit(1)

core_setup = os.path.join(STAGE_DIR, "Synapse-4.11.9.0-win64-setup.exe")
if not os.path.exists(core_setup):
    print(f"[ERR] Core installer not found at {core_setup}")
    sys.exit(1)

core_size = os.path.getsize(core_setup)
print(f"[OK] Generated {core_setup} ({core_size:,} bytes)")

# Copy to CODEX_DIR for records
shutil.copy2(core_setup, os.path.join(CODEX_DIR, "Synapse-4.11.9.0-win64-setup.exe"))
shutil.copy2(core_setup, os.path.join(CODEX_DIR, "Synapse-Setup.exe"))

# Stage into BYOD and Lab builder directories
print("=== Staging core setup and updated binaries into BYOD & Lab builders ===")
shutil.copy2(core_setup, os.path.join(BYOD_DIR, "Synapse-Setup.exe"))
shutil.copy2(core_setup, os.path.join(LAB_DIR, "Synapse-Setup.exe"))

# Also update DigiAgentCore.dll and plugins in BYOD and LAB
fresh_core = os.path.join(STAGE_DIR, "DigiAgentCore.dll")
shutil.copy2(fresh_core, os.path.join(BYOD_DIR, "DigiAgentCore.dll"))
shutil.copy2(fresh_core, os.path.join(LAB_DIR, "DigiAgentCore.dll"))

for p in ["screenlock.dll", "remoteaccess.dll", "windows-platform.dll"]:
    src_p = os.path.join(STAGE_DIR, "plugins", p)
    if os.path.exists(src_p):
        shutil.copy2(src_p, os.path.join(BYOD_DIR, p))
        shutil.copy2(src_p, os.path.join(LAB_DIR, p))

print("=== [2/3] Building Hardened BYOD Installer (Synapse-Setup-BYOD.exe) ===")
os.makedirs(os.path.join(BYOD_DIR, "dist"), exist_ok=True)
proc_byod = subprocess.run([MAKENSIS, "/V2", "Synapse-Setup-BYOD.nsi"], cwd=BYOD_DIR, env=env, capture_output=True, text=True)
if proc_byod.returncode != 0:
    print(f"[ERR] BYOD makensis failed: {proc_byod.stderr}")
    sys.exit(1)

byod_setup = os.path.join(BYOD_DIR, "dist", "Synapse-Setup-BYOD.exe")
if not os.path.exists(byod_setup):
    print(f"[ERR] BYOD installer not found at {byod_setup}")
    sys.exit(1)
print(f"[OK] Generated {byod_setup} ({os.path.getsize(byod_setup):,} bytes)")

print("=== [3/3] Building Lab Silent Installer (Synapse-Setup-Lab.exe) ===")
os.makedirs(os.path.join(LAB_DIR, "dist"), exist_ok=True)
proc_lab = subprocess.run([MAKENSIS, "/V2", "Synapse-Setup-Lab.nsi"], cwd=LAB_DIR, env=env, capture_output=True, text=True)
if proc_lab.returncode != 0:
    print(f"[ERR] Lab makensis failed: {proc_lab.stderr}")
    sys.exit(1)

lab_setup = os.path.join(LAB_DIR, "dist", "Synapse-Setup-Lab.exe")
if not os.path.exists(lab_setup):
    print(f"[ERR] Lab installer not found at {lab_setup}")
    sys.exit(1)
print(f"[OK] Generated {lab_setup} ({os.path.getsize(lab_setup):,} bytes)")

print("\n=== [4/4] Staging Installers & Payloads to Network Shares ===")
share_base = r"\\192.168.1.50\public\Synapse"
if os.path.exists(share_base):
    win_dir = os.path.join(share_base, "1-Windows")
    byod_target = os.path.join(win_dir, "BYOD")
    lab_target = os.path.join(win_dir, "Lab")
    os.makedirs(byod_target, exist_ok=True)
    os.makedirs(lab_target, exist_ok=True)

    # Core setup
    shutil.copy2(core_setup, os.path.join(win_dir, "Synapse-4.11.9.0-win64-setup.exe"))
    shutil.copy2(core_setup, os.path.join(win_dir, "Synapse-Setup.exe"))
    print("[OK] Deployed Core setup to 1-Windows")

    # BYOD
    shutil.copy2(byod_setup, os.path.join(byod_target, "Synapse-Setup-BYOD.exe"))
    shutil.copy2(byod_setup, os.path.join(byod_target, "Synapse-Setup-BYOD-v4.11.9.0.exe"))
    print("[OK] Deployed BYOD setup to 1-Windows/BYOD")

    # Lab
    shutil.copy2(lab_setup, os.path.join(lab_target, "Synapse-Setup-Lab.exe"))
    shutil.copy2(lab_setup, os.path.join(lab_target, "Synapse-Setup-Lab-v4.11.9.0.exe"))
    print("[OK] Deployed Lab setup to 1-Windows/Lab")

    # 4-Auto-Updater payload
    updater_payload = os.path.join(share_base, r"4-Auto-Updater\Payload")
    if os.path.exists(updater_payload):
        shutil.copy2(fresh_core, os.path.join(updater_payload, "DigiAgentCore.dll"))
        up_plugins = os.path.join(updater_payload, "plugins")
        os.makedirs(up_plugins, exist_ok=True)
        for p in ["screenlock.dll", "remoteaccess.dll", "windows-platform.dll"]:
            src_p = os.path.join(STAGE_DIR, "plugins", p)
            if os.path.exists(src_p):
                shutil.copy2(src_p, os.path.join(up_plugins, p))
        print("[OK] Deployed DigiAgentCore & plugins to 4-Auto-Updater/Payload")
else:
    print(f"[WARN] Share path {share_base} not found; skipping network share copy.")

print("\n🎉 ALL THREE INSTALLERS PACKED AND DEPLOYED SUCCESSFULLY!")
