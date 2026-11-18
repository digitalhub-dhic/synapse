import os
import shutil
import hashlib

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()

files = [
    (r"C:\SynapseAISentinel\build_installer\dist\Synapse-Setup-BYOD.exe", r"\\192.168.1.50\public\Synapse\Synapse-Setup-BYOD.exe"),
    (r"C:\SynapseAISentinel\build_installer_lab\dist\Synapse-Setup-Lab.exe", r"\\192.168.1.50\public\Synapse\Synapse-Setup-Lab.exe")
]

for src, dst in files:
    src_hash = sha256(src)
    print(f"Copying {os.path.basename(src)}...")
    shutil.copyfile(src, dst)
    dst_hash = sha256(dst)
    print(f"  Src Hash: {src_hash}")
    print(f"  Dst Hash: {dst_hash}")
    if src_hash == dst_hash:
        print(f"  ✅ VERIFIED 100% INTACT: {os.path.basename(dst)}")
    else:
        print(f"  ❌ HASH MISMATCH for {os.path.basename(dst)}")
        sys.exit(1)

print("\nALL FILES VERIFIED SUCCESSFULLY!")
