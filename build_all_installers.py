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

print("\n🎉 ALL THREE INSTALLERS PACKED AND VERIFIED SUCCESSFULLY!")
