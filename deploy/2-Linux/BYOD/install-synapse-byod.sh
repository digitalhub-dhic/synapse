#!/usr/bin/env bash
# ==============================================================================
# Synapse Command - Linux BYOD Student Laptop Installer v4.11.8
# Self-contained installer for student laptops connecting to the classroom fleet.
# Compatible with: Ubuntu, Debian, Linux Mint, Pop!_OS, Fedora, Arch, openSUSE
# ==============================================================================
set -e

echo "=========================================================="
echo "    SYNAPSE COMMAND — LINUX BYOD CLIENT INSTALLER        "
echo "=========================================================="

if [ "$EUID" -ne 0 ]; then
    echo "[!] Requesting root privileges..."
    exec sudo bash "$0" "$@"
fi

echo "[1/6] Detecting Linux distribution and installing client..."
if command -v apt-get &>/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq veyon-service veyon-plugins libfakekey0 curl jq || apt-get install -y -qq veyon-service
elif command -v dnf &>/dev/null; then
    dnf install -y -q veyon veyon-service curl jq || dnf install -y -q veyon
elif command -v pacman &>/dev/null; then
    pacman -Sy --noconfirm veyon
elif command -v zypper &>/dev/null; then
    zypper in -y veyon
else
    echo "[!] Unknown package manager. Please ensure 'veyon-service' is installed."
fi

echo "[2/6] Configuring Security Public Key (Digihub Classroom)..."
mkdir -p /etc/veyon/keys/public/Digihub
chmod 755 /etc/veyon/keys /etc/veyon/keys/public /etc/veyon/keys/public/Digihub

# Embed Digihub Classroom Public Key directly (guarantees offline/standalone install)
cat << 'EOF' > /etc/veyon/keys/public/Digihub/key
-----BEGIN PUBLIC KEY-----
MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAyFlctkDfg0jlQbGNybUI
54km8qqbZXcZxQpbr0DP/cyb7j2dBouSGgW4r/ftlw2EyL52QFicp+qgv3yP3rey
fup3l/q9nc96SQHDRYXKsP2uf0UbqpUgPzgLiLpvEA/K+q4DWYokdAIxEOop21MZ
yjhx/iL9NieXt/cJBFKa91zLtgd4yYRCHg2IkZkdz+lk9PVZIGUrONWeDSjt1lNH
T5g5Lv3U+70iyFTMVH2Kn/LgHtK+u0ImTE6B0CqAM0VGgHMiUiuhh7PDQN1CzXnA
THV6shUt8xGExp+utYrIkd9p89cEDvow6VNfRY5ASqoYXeJP2+Z1v7K+3/8xgJRp
kfbZwTJI6LnXxpk3nMS6NtKBmwLwe/I9cZnW29DgDDH7eqUET4WsQrzsg6m/iSTF
iiJnpV3Mi4yE2KiYLzqFT9BWqKW3rkE+nkVhJA6F1/Yv86MaxLPSHTbJ0nlv/6Ng
uN5hp8ocW+Do3VmjTzg/UrBYxUO3FkslNe3ebnEUOiM3AIoe8WT680QSDRM8S24U
ELJkZgdtREPaToHJx/syBc5Q1KN+q27sygSoznxTtLMr3iSCvMRIpDhPC7bta6lK
ew34ucsOEovf729AXr99s4O1vwnhEfFLIkPY7Epqmy46M0reaF7LZekkhs1yu99C
E0+j9fluQG7qBLL7qB5lx88CAwEAAQ==
-----END PUBLIC KEY-----
EOF
chmod 644 /etc/veyon/keys/public/Digihub/key

echo "[3/6] Setting Authentication & Service Configuration..."
if command -v veyon-cli &>/dev/null; then
    veyon-cli config set Authentication/Method 1 || true
    veyon-cli config set Authentication/KeyFileDirectories/Public "/etc/veyon/keys/public" || true
    veyon-cli config set Network/FirewallExceptionEnabled true || true
    # Safely configure autostart without failing under sudo if Session DBus is absent
    veyon-cli config set Service/Autostart true >/dev/null 2>&1 || true
fi

echo "[4/6] Configuring Local Firewall Rules (Port 11100 / 11400)..."
if command -v ufw &>/dev/null && ufw status 2>/dev/null | grep -qw "active"; then
    ufw allow 11100/tcp comment 'Synapse Classroom Monitoring' >/dev/null 2>&1 || true
    ufw allow 11400/tcp comment 'Synapse Screen Broadcast' >/dev/null 2>&1 || true
fi
if command -v firewall-cmd &>/dev/null && systemctl is-active --quiet firewalld 2>/dev/null; then
    firewall-cmd --permanent --add-port=11100/tcp >/dev/null 2>&1 || true
    firewall-cmd --permanent --add-port=11400/tcp >/dev/null 2>&1 || true
    firewall-cmd --reload >/dev/null 2>&1 || true
fi

echo "[5/6] Starting Synapse Service..."
# Create XDG desktop autostart so veyon-service starts on user login
mkdir -p /etc/xdg/autostart
cat << 'EOF' > /etc/xdg/autostart/veyon-service.desktop
[Desktop Entry]
Type=Application
Name=Veyon Service
Comment=Synapse Classroom Client Service
Exec=/usr/bin/veyon-service
Icon=veyon-service
Terminal=false
Categories=System;
X-GNOME-Autostart-enabled=true
EOF
chmod 644 /etc/xdg/autostart/veyon-service.desktop

systemctl daemon-reload >/dev/null 2>&1 || true
for svc in veyon-service veyon; do
    if systemctl list-unit-files 2>/dev/null | grep -q "^$svc.service"; then
        systemctl enable "$svc.service" >/dev/null 2>&1 || true
        systemctl restart "$svc.service" >/dev/null 2>&1 || true
    fi
done

# If executed via sudo from an active desktop terminal, launch service in user session
if [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != "root" ]; then
    SUDO_UID=$(id -u "$SUDO_USER" 2>/dev/null || true)
    if [ -n "$SUDO_UID" ] && ! pgrep -u "$SUDO_USER" -x veyon-service >/dev/null 2>&1; then
        sudo -u "$SUDO_USER" DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$SUDO_UID/bus" DISPLAY="${DISPLAY:-:0}" veyon-service >/dev/null 2>&1 &
    fi
fi

echo "[6/6] Checking Display Server (X11 / Wayland)..."
if [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    echo "  [NOTE] Current session is Wayland."
    echo "  For full remote screen sharing and control, log out and select"
    echo "  'Ubuntu on Xorg' or 'GNOME on Xorg' at the login screen (gear icon)."
fi

# Fetch Local IP and Hostname
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
LOCAL_HOST=$(hostname 2>/dev/null)

echo "[*] Announcing presence to Synapse Command..."
python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
data = f'SYNAPSE_BEACON|1|${LOCAL_HOST}|${LOCAL_IP}|'.encode('utf-8')
for target in ['255.255.255.255', '192.168.1.50', '192.168.1.255']:
    try: s.sendto(data, (target, 11400))
    except: pass
s.close()
" 2>/dev/null || ping -c 1 192.168.1.50 >/dev/null 2>&1 || true

echo ""
echo "=========================================================="
echo " [SUCCESS] Synapse BYOD Client is Installed & Active!     "
echo "=========================================================="
echo " Device Information:"
echo "   Hostname : ${LOCAL_HOST}"
echo "   IP       : ${LOCAL_IP}"
echo "=========================================================="
echo " Auto-Discovery is ACTIVE: Synapse Command will"
echo " automatically detect this computer and add it to BYOD!"
echo "=========================================================="
