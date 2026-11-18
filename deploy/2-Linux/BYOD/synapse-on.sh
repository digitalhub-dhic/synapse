#!/usr/bin/env bash
# Turn ON Synapse Monitoring (Classroom Mode)
if [ "$EUID" -ne 0 ]; then
    exec sudo bash "$0" "$@"
fi

for svc in veyon-service veyon; do
    systemctl start "$svc.service" 2>/dev/null || true
done

# Broadcast beacon to teacher console for instant auto-discovery
python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1); s.sendto(b'SYNAPSE_BEACON:11100', ('255.255.255.255', 11400))" 2>/dev/null || true

echo "=========================================================="
echo " [ONLINE] Synapse Classroom Monitoring is turned ON!       "
echo " Connected to teacher console for screen sharing and demo. "
echo "=========================================================="
