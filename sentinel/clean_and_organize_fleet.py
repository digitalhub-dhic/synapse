import subprocess
import json
import uuid

CLI = r"C:\Program Files\DigiAgent\digiagent-cli.exe"

def clean_fleet():
    # 1. Fetch current config
    res = subprocess.run([CLI, "config", "get", "BuiltinDirectory/NetworkObjects"], capture_output=True, text=True)
    try:
        raw_data = json.loads(res.stdout)
    except Exception as e:
        print("[ERR] Could not parse current objects:", e)
        return

    # Location UUIDs
    LOC_DIGIHUB = "{9d17e6e9-ad44-559a-8362-717e23722df6}"
    LOC_BYOD = "{2811d91e-01d0-56aa-a1fb-f84c33085ec2}"
    LOC_RESERVE = "{98e6135a-2831-5bd4-8324-fa6ba4791e77}"

    clean_objects = [
        {"Name": "DIGIHUB", "Type": 2, "Uid": LOC_DIGIHUB},
        {"Name": "BYOD", "Type": 2, "Uid": LOC_BYOD},
        {"Name": "special reserve", "Type": 2, "Uid": LOC_RESERVE},
    ]

    seen_ips = set()
    seen_names = set()

    # Teacher / Master IPs and Hostnames to NEVER monitor
    FORBIDDEN_HOSTS = {"192.168.1.150", "100.99.161.43", "127.0.0.1"}
    FORBIDDEN_NAMES = {"computer-50", "computer-50.digihub.net"}

    # Separate existing computers
    computers = [x for x in raw_data if x.get("Type") == 3]

    for c in computers:
        name = c.get("Name", "").strip()
        host = c.get("HostAddress", "").strip()
        mac = c.get("MacAddress", "").strip().upper()
        uid = c.get("Uid", f"{{{uuid.uuid4()}}}")

        # Skip forbidden teacher machine
        if host in FORBIDDEN_HOSTS or name.lower() in FORBIDDEN_NAMES or host.startswith("169.254."):
            print(f"[PURGE-TEACHER/APIPA] Removing {name} ({host})")
            continue

        # Skip duplicate IP or duplicate Name
        norm_host = host.lower()
        norm_name = name.lower()
        if norm_host in seen_ips:
            print(f"[PURGE-DUPLICATE-IP] Removing duplicate host {host} for {name}")
            continue
        if norm_name in seen_names:
            print(f"[PURGE-DUPLICATE-NAME] Removing duplicate name {name} ({host})")
            continue

        seen_ips.add(norm_host)
        seen_names.add(norm_name)

        # STRICT BYOD vs LAB GROUPING:
        # A device belongs in BYOD ONLY if:
        # - It is on the student Wi-Fi subnet: 192.168.30.x
        # - It is on Tailscale mesh: 100.x.x.x
        # - Or it was registered as BYOD-*
        # All 192.168.1.x devices belong in DIGIHUB (or special reserve)
        if norm_name == "al midad" or host == "192.168.1.137":
            parent = LOC_RESERVE
            dir_addr = "special reserve"
        elif host.startswith("192.168.30.") or host.startswith("100.") or norm_name.startswith("byod-"):
            parent = LOC_BYOD
            dir_addr = "BYOD"
        else:
            parent = LOC_DIGIHUB
            dir_addr = "DIGIHUB"

        entry = {
            "HostAddress": host,
            "Name": name,
            "ParentUid": parent,
            "Type": 3,
            "Uid": uid
        }
        if mac:
            entry["MacAddress"] = mac
        if dir_addr:
            entry["DirectoryAddress"] = dir_addr

        clean_objects.append(entry)

    # Serialize and save to config
    new_json_str = json.dumps(clean_objects, separators=(',', ':'))
    set_res = subprocess.run([CLI, "config", "set", "BuiltinDirectory/NetworkObjects", new_json_str], capture_output=True, text=True)
    print("[OK] Saved cleaned network objects. Returncode:", set_res.returncode)

    # Set update interval to 10 seconds for real-time fleet refresh
    subprocess.run([CLI, "config", "set", "NetworkObjectDirectory/UpdateInterval", "10"], capture_output=True)
    print("[OK] Set NetworkObjectDirectory/UpdateInterval = 10s")

    # Print summary
    by_loc = {}
    for x in clean_objects:
        if x.get("Type") == 3:
            p = x.get("ParentUid")
            loc_label = "DIGIHUB" if p == LOC_DIGIHUB else ("BYOD" if p == LOC_BYOD else "RESERVE")
            by_loc.setdefault(loc_label, []).append(f"{x['Name']} ({x['HostAddress']})")

    print(f"\n--- CLEAN FLEET SUMMARY ---")
    for loc, hosts in by_loc.items():
        print(f"[{loc}] ({len(hosts)} devices):")
        for h in hosts[:10]:
            print(f"   • {h}")
        if len(hosts) > 10:
            print(f"   ... and {len(hosts)-10} more")

if __name__ == "__main__":
    clean_fleet()
