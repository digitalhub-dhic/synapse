#!/usr/bin/env bash
# Turn ON Synapse Monitoring (Classroom Mode)
set -euo pipefail
STATE_DIR="/var/lib/synapse"
RELEASE_MARKER="$STATE_DIR/monitoring-released"
if [ "$EUID" -ne 0 ]; then
    exec sudo bash "$0" "$@"
fi

found=0
for svc in veyon-service veyon; do
    if systemctl list-unit-files "$svc.service" --no-legend 2>/dev/null | grep -q "^$svc.service"; then
        found=1
        systemctl enable --now "$svc.service"
        if ! systemctl is-active --quiet "$svc.service"; then
            echo "[ERROR] $svc.service did not become active; release state remains in place." >&2
            exit 1
        fi
    fi
done
if [ "$found" -eq 0 ]; then
    echo "[ERROR] No Veyon system service is installed; monitoring cannot be restored." >&2
    exit 1
fi

# Clear release state only after the system service is confirmed active.
rm -f "$RELEASE_MARKER"

# Broadcast beacon to teacher console for instant auto-discovery
TEACHER_LAN_IP="192.168.1.150"
TEACHER_TAILSCALE_IP="100.99.161.43"
if [ -r /etc/synapse/teacher-endpoints.conf ]; then . /etc/synapse/teacher-endpoints.conf; fi
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}' || true)
LOCAL_HOST=$(hostname 2>/dev/null || true)
python3 - "$LOCAL_HOST" "$LOCAL_IP" "$TEACHER_LAN_IP" "$TEACHER_TAILSCALE_IP" <<'PY' 2>/dev/null || true
import json, socket, sys
host, ip, teacher_lan, teacher_tail = sys.argv[1:5]
payload = json.dumps({'hostname': host, 'ip': ip, 'mac': ''}).encode('utf-8')
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
for target in ('255.255.255.255', teacher_lan, teacher_tail):
    try:
        sock.sendto(payload, (target, 11105))
    except OSError:
        pass
sock.close()
PY

echo "=========================================================="
echo " [REVOKED] Veyon service is active and release state cleared."
echo " Confirm live capture/control in the teacher console.       "
echo "=========================================================="
