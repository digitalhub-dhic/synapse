#!/bin/bash
# ==============================================================================
# Synapse Command — Universal Linux Client Installer
# Compatible with: Ubuntu, Debian, Linux Mint, Fedora, RHEL, CentOS, Arch Linux
# ==============================================================================

set -e

# Terminal Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}================================================================${NC}"
echo -e "${GREEN}   SYNAPSE COMMAND — UNIVERSAL LINUX AGENT INSTALLER           ${NC}"
echo -e "${GREEN}================================================================${NC}"

# Check for root / sudo
if [ "$EUID" -ne 0 ]; then
  echo -e "${RED}[ERROR] Please run this installer as root (e.g. sudo bash install-agent.sh)${NC}"
  exit 1
fi

# Detect Package Manager
echo -e "${BLUE}[1/5] Detecting Linux distribution and package manager...${NC}"
if command -v apt-get &> /dev/null; then
    PKG_MGR="apt"
    echo -e "      Found Debian/Ubuntu-based distribution (${PKG_MGR})"
elif command -v dnf &> /dev/null; then
    PKG_MGR="dnf"
    echo -e "      Found Fedora/RHEL-based distribution (${PKG_MGR})"
elif command -v yum &> /dev/null; then
    PKG_MGR="yum"
    echo -e "      Found RHEL/CentOS-based distribution (${PKG_MGR})"
elif command -v pacman &> /dev/null; then
    PKG_MGR="pacman"
    echo -e "      Found Arch Linux-based distribution (${PKG_MGR})"
else
    echo -e "${YELLOW}[WARN] Unknown package manager. Assuming manual dependencies.${NC}"
    PKG_MGR="unknown"
fi

# Install Dependencies
echo -e "${BLUE}[2/5] Installing core dependencies (Python 3, x11vnc, xdotool, zenity)...${NC}"
case $PKG_MGR in
    apt)
        apt-get update -y -qq
        apt-get install -y -qq python3 python3-pip x11vnc xdotool zenity libnotify-bin net-tools curl
        ;;
    dnf)
        dnf install -y -q python3 python3-pip x11vnc xdotool zenity libnotify net-tools curl
        ;;
    yum)
        yum install -y -q python3 python3-pip x11vnc xdotool zenity libnotify net-tools curl
        ;;
    pacman)
        pacman -Sy --noconfirm python x11vnc xdotool zenity libnotify net-tools curl
        ;;
    *)
        echo "      Skipping package install step."
        ;;
esac

# Create Installation Directories
echo -e "${BLUE}[3/5] Deploying Synapse Agent binaries and configuration...${NC}"
mkdir -p /opt/synapse-agent
mkdir -p /etc/synapse
mkdir -p /var/log

# Copy Agent Script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/synapse_linux_agent.py" ]; then
    cp "$SCRIPT_DIR/synapse_linux_agent.py" /opt/synapse-agent/synapse_linux_agent.py
else
    # In case curl pipe installer is used, write directly
    echo -e "${RED}[ERROR] synapse_linux_agent.py not found in source directory!${NC}"
    exit 1
fi
chmod +x /opt/synapse-agent/synapse_linux_agent.py

# Create Default Config if not exists
if [ ! -f /etc/synapse/agent.conf ]; then
    if [ -f "$SCRIPT_DIR/agent.conf" ]; then
        cp "$SCRIPT_DIR/agent.conf" /etc/synapse/agent.conf
    else
        cat << 'EOF' > /etc/synapse/agent.conf
{
  "master_host": "192.168.1.150",
  "master_port": 8080,
  "room_name": "Computer Lab 01",
  "heartbeat_interval_sec": 3,
  "disconnect_lock_timeout_sec": 12,
  "vnc_port": 11100,
  "enable_auto_lock_on_disconnect": true
}
EOF
    fi
    chmod 644 /etc/synapse/agent.conf
    echo -e "      Created configuration at /etc/synapse/agent.conf"
else
    echo -e "      Preserving existing configuration at /etc/synapse/agent.conf"
fi

# Install systemd Service
echo -e "${BLUE}[4/5] Installing and configuring systemd background service...${NC}"
if [ -f "$SCRIPT_DIR/synapse-agent.service" ]; then
    cp "$SCRIPT_DIR/synapse-agent.service" /etc/systemd/system/synapse-agent.service
else
    cat << 'EOF' > /etc/systemd/system/synapse-agent.service
[Unit]
Description=Synapse Command — Linux Workstation Client Daemon
After=network-online.target graphical.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 /opt/synapse-agent/synapse_linux_agent.py
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal
User=root
Group=root
Environment="DISPLAY=:0"

[Install]
WantedBy=multi-user.target
EOF
fi

chmod 644 /etc/systemd/system/synapse-agent.service
systemctl daemon-reload
systemctl enable synapse-agent.service
systemctl restart synapse-agent.service

# Verification
echo -e "${BLUE}[5/5] Verifying service status...${NC}"
sleep 2

if systemctl is-active --quiet synapse-agent.service; then
    echo -e "${GREEN}================================================================${NC}"
    echo -e "${GREEN}   SUCCESS: Synapse Linux Agent is active and running!          ${NC}"
    echo -e "${GREEN}================================================================${NC}"
    echo -e "  • Service:   synapse-agent.service (Enabled on boot)"
    echo -e "  • Config:    /etc/synapse/agent.conf"
    echo -e "  • Log file:  journalctl -u synapse-agent -f"
    echo -e "  • VNC Port:  11100"
    echo -e "  • Auto-Lock: Active on disconnect"
else
    echo -e "${YELLOW}[WARN] Service installed, but may need graphical login. Check:${NC}"
    echo -e "       systemctl status synapse-agent.service"
fi
