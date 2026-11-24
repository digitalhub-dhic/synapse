#!/usr/bin/env bash
# Turn OFF Synapse Monitoring (Student Privacy Mode)
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
        systemctl disable --now "$svc.service"
        if systemctl is-active --quiet "$svc.service"; then
            echo "[ERROR] $svc.service is still active; monitoring release was not recorded." >&2
            exit 1
        fi
    fi
done
if [ "$found" -eq 0 ]; then
    echo "[ERROR] No Veyon system service is installed; cannot verify monitoring is off." >&2
    exit 1
fi

# Retire legacy installs of the custom root-level x11vnc agent as well.
if [ -f /etc/systemd/system/synapse-agent.service ]; then
    systemctl disable --now synapse-agent.service
    rm -f /etc/systemd/system/synapse-agent.service
    systemctl daemon-reload
fi

install -d -m 0755 "$STATE_DIR"
tmp_marker=$(mktemp "$STATE_DIR/.monitoring-released.XXXXXX")
printf 'released\n' > "$tmp_marker"
chmod 0644 "$tmp_marker"
mv -f "$tmp_marker" "$RELEASE_MARKER"

echo "=========================================================="
echo " [RELEASED] Synapse monitoring is turned OFF until revoked."
echo " Veyon screen sharing and remote control are disabled."
echo " Tailscale remains connected until separately disconnected."
echo "=========================================================="
