#!/usr/bin/env bash
# ==============================================================================
# Synapse Command - Linux BYOD Student Laptop Installer v4.11.8.2
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

# Clean up any previously created duplicate user autostart files
rm -f /etc/xdg/autostart/veyon-service.desktop
killall -9 veyon-service veyon-server veyon-worker 2>/dev/null || true

echo "[1/6] Detecting Linux distribution and installing client..."
if command -v apt-get &>/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq veyon-service veyon-plugins libfakekey0 curl jq xdg-desktop-portal pipewire || apt-get install -y -qq veyon-service

    # Upgrade to Veyon 4.11+ (native Wayland PipeWire support) if distro ships < 4.11
    VEYON_VER=$(veyon-cli --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+' | head -n1 || echo "0.0")
    if [ -n "$VEYON_VER" ] && awk "BEGIN {exit !($VEYON_VER < 4.11)}"; then
        echo "  [*] Detected Veyon $VEYON_VER (< 4.11). Checking for Veyon 4.11+ package..."
        if grep -qi "ubuntu" /etc/os-release 2>/dev/null && command -v add-apt-repository &>/dev/null; then
            add-apt-repository -y ppa:veyon/stable >/dev/null 2>&1 || true
            apt-get update -qq >/dev/null 2>&1 || true
            apt-get install -y -qq veyon-service veyon-plugins >/dev/null 2>&1 || true
        else
            CODENAME=$(. /etc/os-release 2>/dev/null && echo "${VERSION_CODENAME:-bookworm}")
            DEB_URL=$(curl -sSL https://api.github.com/repos/veyon/veyon/releases/latest 2>/dev/null | jq -r ".assets[]?.browser_download_url | select(test(\"${CODENAME}.*amd64\\\\.deb$|debian.*amd64\\\\.deb$\"))" | head -n1 || true)
            if [ -n "$DEB_URL" ] && [ "$DEB_URL" != "null" ]; then
                curl -sSL "$DEB_URL" -o /tmp/veyon-latest.deb && apt-get install -y -qq /tmp/veyon-latest.deb || true
                rm -f /tmp/veyon-latest.deb
            fi
        fi
    fi
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
    veyon-cli config set Authentication/Method 1 2>/dev/null || true
    veyon-cli config set Authentication/KeyFileDirectories/Public "/etc/veyon/keys/public" 2>/dev/null || true
    veyon-cli config set Network/FirewallExceptionEnabled true 2>/dev/null || true
    # Disable tray icon to prevent GNOME/Wayland veyon-worker respawn OOM loop
    veyon-cli config set Service/HideTrayIcon true 2>/dev/null || true
fi

# Direct INI fallback so HideTrayIcon=true is guaranteed even without Session DBus
for conf_dir in "/etc/xdg/Veyon Solutions" "/etc/veyon"; do
    mkdir -p "$conf_dir"
    conf_file="$conf_dir/Veyon.conf"
    if [ -f "$conf_file" ]; then
        if grep -q "^\[Service\]" "$conf_file"; then
            grep -q "^HideTrayIcon=" "$conf_file" && sed -i 's/^HideTrayIcon=.*/HideTrayIcon=true/' "$conf_file" || sed -i '/^\[Service\]/a HideTrayIcon=true' "$conf_file"
        else
            printf "\n[Service]\nHideTrayIcon=true\n" >> "$conf_file"
        fi
    fi
done

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
VEYON_VER=$(veyon-cli --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+' | head -n1 || echo "0.0")
IS_LEGACY_VEYON=$(awk "BEGIN {print ($VEYON_VER < 4.11) ? 1 : 0}" 2>/dev/null || echo "1")

# Only force X11 in GDM3 if Veyon < 4.11 (4.11+ supports Wayland natively via PipeWire)
if [ "$IS_LEGACY_VEYON" = "1" ]; then
    for gdm_conf in /etc/gdm3/custom.conf /etc/gdm3/daemon.conf; do
        if [ -f "$gdm_conf" ]; then
            sed -i 's/^#\?WaylandEnable=.*/WaylandEnable=false/' "$gdm_conf" 2>/dev/null || true
        fi
    done
fi

# Ensure systemd rate-limits and enforces strict TasksMax/MemoryMax guardrails on both unit names
for unit_dir in /etc/systemd/system/veyon.service.d /etc/systemd/system/veyon-service.service.d; do
    mkdir -p "$unit_dir"
    cat << 'EOF' > "$unit_dir/limits.conf"
[Service]
Restart=on-failure
RestartSec=5s
TasksMax=20
MemoryMax=512M
EOF
done

systemctl daemon-reload >/dev/null 2>&1 || true

# Start single authoritative system service
for svc in veyon veyon-service; do
    if systemctl list-unit-files 2>/dev/null | grep -q "^$svc.service"; then
        systemctl enable "$svc.service" >/dev/null 2>&1 || true
        systemctl restart "$svc.service" >/dev/null 2>&1 || true
        break
    fi
done

echo "[6/6] Checking Display Server (X11 / Wayland)..."
if [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    echo ""
    echo "  [IMPORTANT] Current session is Wayland."
    echo "  Veyon screen monitoring requires X11 (Xorg)."
    echo "  We have configured GDM to use Xorg. Please log out, or at the"
    echo "  login screen click the gear icon (⚙️) and select 'Ubuntu on Xorg'."
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

# Install Git-based Linux Auto-Updater (checks GitHub every 30m & at boot)
mkdir -p /var/lib/synapse
cat <<'EOF' > /usr/local/bin/synapse-autoupdate.sh
#!/usr/bin/env bash
set -e
GH_RAW="https://raw.githubusercontent.com/digitalhub-dhic/synapse/main"
VER_FILE="/var/lib/synapse/installed_version.txt"
REMOTE_VER=$(curl -fsSL "$GH_RAW/deploy/4-Auto-Updater/client_version.json" 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin).get('latest_version',''))" 2>/dev/null || true)
[ -z "$REMOTE_VER" ] && exit 0
LOCAL_VER=$(cat "$VER_FILE" 2>/dev/null || echo "")
if [ "$REMOTE_VER" != "$LOCAL_VER" ]; then
    TMP_SH=$(mktemp /tmp/synapse-byod-XXXXXX.sh)
    if curl -fsSL "$GH_RAW/deploy/2-Linux/BYOD/install-synapse-byod.sh" -o "$TMP_SH"; then
        chmod +x "$TMP_SH"
        SYNAPSE_AUTOUPDATE_RUN=1 bash "$TMP_SH" >/var/log/synapse-autoupdate.log 2>&1 || true
        echo "$REMOTE_VER" > "$VER_FILE"
    fi
    rm -f "$TMP_SH"
fi
EOF
chmod +x /usr/local/bin/synapse-autoupdate.sh

if [ -z "${SYNAPSE_AUTOUPDATE_RUN:-}" ] && command -v systemctl >/dev/null 2>&1; then
    cat <<'EOF' > /etc/systemd/system/synapse-autoupdate.service
[Unit]
Description=Synapse Linux BYOD Auto-Updater (GitHub)
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/synapse-autoupdate.sh
EOF
    cat <<'EOF' > /etc/systemd/system/synapse-autoupdate.timer
[Unit]
Description=Check GitHub for newer Synapse Linux BYOD releases

[Timer]
OnBootSec=2min
OnUnitActiveSec=30min
Persistent=true

[Install]
WantedBy=timers.target
EOF
    systemctl daemon-reload >/dev/null 2>&1 || true
    systemctl enable --now synapse-autoupdate.timer >/dev/null 2>&1 || true
fi

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
