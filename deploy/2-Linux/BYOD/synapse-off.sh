#!/usr/bin/env bash
# Turn OFF Synapse Monitoring (Student Privacy Mode)
if [ "$EUID" -ne 0 ]; then
    exec sudo bash "$0" "$@"
fi

for svc in veyon-service veyon; do
    systemctl stop "$svc.service" 2>/dev/null || true
done

echo "=========================================================="
echo " [PRIVACY ON] Synapse Classroom Monitoring is turned OFF! "
echo " No screen sharing, remote control, or network monitoring."
echo "=========================================================="
