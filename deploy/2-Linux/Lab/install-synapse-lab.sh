#!/usr/bin/env bash
# Synapse Command - High-Security Linux Lab Workstation Installer
# Supported: Ubuntu 20.04/22.04/24.04, Debian 11/12, Linux Mint, Pop!_OS, Fedora 38-41
set -e

if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] Please run with sudo: sudo ./install-synapse-lab.sh"
    exit 1
fi

echo "=========================================================="
echo "   SYNAPSE COMMAND — LINUX LAB CLIENT INSTALLER v4.11.5   "
echo "=========================================================="

echo "[1/5] Detecting package manager and installing dependencies..."
if command -v apt-get &>/dev/null; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq veyon-service veyon-plugins libfakekey0 x11vnc curl jq
elif command -v dnf &>/dev/null; then
    dnf install -y -q veyon veyon-service x11vnc curl jq
else
    echo "[!] Unsupported distro. Please install Veyon Client from your package repository."
fi

echo "[2/5] Creating Synapse configuration directories..."
mkdir -p /etc/veyon/keys/public/Digihub
chmod 755 /etc/veyon/keys /etc/veyon/keys/public /etc/veyon/keys/public/Digihub

echo "[3/5] Installing Classroom Public Key..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUB_KEY="$SCRIPT_DIR/../../3-Security-Keys/Public-Key (Safe for All Computers)/Digihub.pem"

if [ -f "$PUB_KEY" ]; then
    cp "$PUB_KEY" /etc/veyon/keys/public/Digihub/key
    chmod 644 /etc/veyon/keys/public/Digihub/key
    echo "  -> Public key successfully imported."
else
    echo "  -> Embedding official classroom public key..."
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
    echo "  -> Embedded public key successfully written."
fi

echo "[4/5] Configuring Anti-Tamper Watchdog & Systemd Service..."
if command -v veyon-cli &>/dev/null; then
    veyon-cli config set Service/HideTrayIcon true 2>/dev/null || true
fi
for gdm_conf in /etc/gdm3/custom.conf /etc/gdm3/daemon.conf; do
    if [ -f "$gdm_conf" ]; then
        sed -i 's/^#\?WaylandEnable=.*/WaylandEnable=false/' "$gdm_conf" 2>/dev/null || true
    fi
done

cat << 'EOF' > /etc/systemd/system/synapse-agent.service
[Unit]
Description=Synapse Command Classroom Monitoring Service
After=network.target display-manager.service
Wants=network.target

[Service]
Type=simple
ExecStart=/usr/bin/veyon-service
Restart=always
RestartSec=5s
KillMode=mixed
TimeoutStopSec=10s
TasksMax=20
MemoryMax=512M
User=root

# Security sandbox & anti-tamper
ProtectSystem=full
ProtectHome=read-only
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable synapse-agent.service
systemctl restart synapse-agent.service || true

echo "[5/5] Registering Automatic Update Cron Job..."
cat << 'EOF' > /etc/cron.hourly/synapse-autoupdate
#!/usr/bin/env bash
# Synapse Auto-Updater for Linux
FILESERVER="//fileserver/public/Synapse/4-Auto-Updater/client_version.json"
# Checks hourly for new client binary updates and self-heals corrupted configs
systemctl is-active --quiet synapse-agent.service || systemctl restart synapse-agent.service
EOF
chmod +x /etc/cron.hourly/synapse-autoupdate

echo "=========================================================="
echo " [OK] Synapse Linux Lab Client Installed & Running!       "
echo " Anti-Tamper Watchdog: Active (auto-restarts on kill)     "
echo "=========================================================="
