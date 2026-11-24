#!/usr/bin/env bash
# Synapse Command - High-Security Linux Lab Workstation Installer
# Supported: Ubuntu 20.04/22.04/24.04, Debian 11/12, Linux Mint, Pop!_OS, Fedora 38-41
set -e

MIN_VEYON_VERSION="4.11.5"
TEACHER_LAN_IP="${SYNAPSE_TEACHER_LAN_IP:-192.168.1.150}"
TEACHER_TAILSCALE_IP="${SYNAPSE_TEACHER_TAILSCALE_IP:-100.99.161.43}"

if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] Please run with sudo: sudo ./install-synapse-lab.sh"
    exit 1
fi

echo "=========================================================="
echo "   SYNAPSE COMMAND — LINUX LAB CLIENT INSTALLER v4.11.5   "
echo "=========================================================="

if ! command -v systemctl >/dev/null 2>&1; then
    echo "[ERROR] A systemd host is required to manage the Veyon service safely." >&2
    exit 1
fi
MASKED_UNITS=()
SETUP_COMPLETE=0
cleanup_runtime_masks() {
    for unit in "${MASKED_UNITS[@]}"; do
        systemctl unmask --runtime "$unit" >/dev/null 2>&1 || true
    done
}
cleanup_failed_install() {
    if [ "$SETUP_COMPLETE" -ne 1 ]; then
        for unit in veyon-service.service veyon.service; do
            systemctl disable --now "$unit" >/dev/null 2>&1 || true
        done
    fi
    cleanup_runtime_masks
}
trap cleanup_failed_install EXIT
for unit in veyon-service.service veyon.service; do
    if ! systemctl is-enabled "$unit" 2>/dev/null | grep -Eq '^masked(-runtime)?$'; then
        systemctl mask --runtime "$unit" >/dev/null
        MASKED_UNITS+=("$unit")
    fi
    if systemctl list-unit-files "$unit" --no-legend 2>/dev/null | grep -q "^$unit"; then
        systemctl stop "$unit" >/dev/null 2>&1 || true
    fi
done

echo "[1/5] Detecting package manager and installing dependencies..."
if command -v apt-get &>/dev/null; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq veyon-service veyon-plugins libfakekey0 curl jq
elif command -v dnf &>/dev/null; then
    dnf install -y -q veyon veyon-service curl jq
else
    echo "[!] Unsupported distro. Please install Veyon Client from your package repository."
fi

if ! command -v veyon-cli >/dev/null 2>&1; then
    echo "[ERROR] veyon-cli was not installed. Install Veyon ${MIN_VEYON_VERSION} or newer and rerun this installer."
    exit 1
fi
VEYON_VERSION=$(veyon-cli --version 2>&1 | sed -nE 's/.*([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' | head -n 1)
if [ -z "$VEYON_VERSION" ] || [ "$(printf '%s\n' "$MIN_VEYON_VERSION" "$VEYON_VERSION" | sort -V | head -n 1)" != "$MIN_VEYON_VERSION" ]; then
    echo "[ERROR] Veyon ${MIN_VEYON_VERSION} or newer is required for supported Wayland capture and control. Found: ${VEYON_VERSION:-unknown}."
    echo "Install a current Veyon package from the official Veyon download source, then rerun this installer."
    exit 1
fi

python3 - "$TEACHER_LAN_IP" "$TEACHER_TAILSCALE_IP" <<'PY'
import ipaddress, sys
for value in sys.argv[1:]:
    ipaddress.IPv4Address(value)
PY

echo "[2/5] Creating Synapse configuration directories..."
mkdir -p /etc/veyon/keys/public/Digihub
chmod 755 /etc/veyon/keys /etc/veyon/keys/public /etc/veyon/keys/public/Digihub

echo "[3/5] Installing Classroom Public Key..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK_SCRIPT="$SCRIPT_DIR/../Tools/synapse-linux-check.sh"
if [ ! -f "$CHECK_SCRIPT" ]; then CHECK_SCRIPT="$SCRIPT_DIR/synapse-linux-check.sh"; fi
if [ -f "$CHECK_SCRIPT" ]; then
    install -m 0755 "$CHECK_SCRIPT" /usr/local/bin/synapse-linux-check
fi
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

veyon-cli config set Authentication/Method 1
veyon-cli config set Authentication/KeyFileDirectories/Public "/etc/veyon/keys/public"
veyon-cli config set Service/Autostart true

echo "[4/6] Restricting Veyon access to teacher endpoints..."
install -d -m 0755 /etc/synapse
cat > /etc/synapse/teacher-endpoints.conf <<EOF
TEACHER_LAN_IP=$TEACHER_LAN_IP
TEACHER_TAILSCALE_IP=$TEACHER_TAILSCALE_IP
EOF
chmod 0644 /etc/synapse/teacher-endpoints.conf

if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -qw "active"; then
    while true; do
        rule_number=$(ufw status numbered | awk '/# Synapse Teacher LAN|# Synapse Teacher Tailscale|# Restrict Synapse|# Synapse Classroom Monitoring|# Synapse Screen Broadcast/ { n=$1; gsub(/[^0-9]/, "", n); if (!n) { n=$2; gsub(/[^0-9]/, "", n) }; if (n) { print n; exit } }')
        [ -n "$rule_number" ] || break
        ufw --force delete "$rule_number"
    done
    ufw insert 1 allow from "$TEACHER_LAN_IP" to any port 11100 proto tcp comment 'Synapse Teacher LAN 11100'
    ufw insert 2 allow from "$TEACHER_TAILSCALE_IP" to any port 11100 proto tcp comment 'Synapse Teacher Tailscale 11100'
    ufw insert 3 allow from "$TEACHER_LAN_IP" to any port 11400 proto tcp comment 'Synapse Teacher LAN 11400'
    ufw insert 4 allow from "$TEACHER_TAILSCALE_IP" to any port 11400 proto tcp comment 'Synapse Teacher Tailscale 11400'
    ufw insert 5 deny 11100/tcp comment 'Restrict Synapse 11100 to teacher endpoints'
    ufw insert 6 deny 11400/tcp comment 'Restrict Synapse 11400 to teacher endpoints'
fi
if command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active --quiet firewalld 2>/dev/null; then
    if firewall-cmd --permanent --query-service=veyon >/dev/null 2>&1; then
        firewall-cmd --permanent --remove-service=veyon
    fi
    for port in 11100/tcp 11400/tcp; do
        if firewall-cmd --permanent --query-port="$port" >/dev/null 2>&1; then
            firewall-cmd --permanent --remove-port="$port"
        fi
    done
    expected_rules=()
    for teacher_ip in "$TEACHER_LAN_IP" "$TEACHER_TAILSCALE_IP"; do
        for port in 11100 11400; do
            rule="rule family=ipv4 source address=${teacher_ip}/32 port port=${port} protocol=tcp accept"
            expected_rules+=("$rule")
            if ! firewall-cmd --permanent --query-rich-rule="$rule" >/dev/null 2>&1; then
                firewall-cmd --permanent --add-rich-rule="$rule"
            fi
        done
    done
    while IFS= read -r rule; do
        [[ "$rule" == *"protocol=tcp"*"accept"* ]] || continue
        port_spec=$(sed -n 's/.*port port=\([^ ]*\).*/\1/p' <<<"$rule")
        port_spec="${port_spec%/tcp}"
        covers_veyon_port=0
        if [[ "$port_spec" =~ ^([0-9]+)-([0-9]+)$ ]]; then
            low=${BASH_REMATCH[1]}; high=${BASH_REMATCH[2]}
            for managed_port in 11100 11400; do
                if (( low <= managed_port && managed_port <= high )); then covers_veyon_port=1; fi
            done
        elif [[ "$port_spec" = 11100 || "$port_spec" = 11400 ]]; then
            covers_veyon_port=1
        fi
        [ "$covers_veyon_port" -eq 1 ] || continue
        if [[ ! " ${expected_rules[*]} " == *" $rule "* ]]; then
            echo "[ERROR] Unexpected firewalld Veyon accept rule exists: $rule" >&2
            exit 1
        fi
    done < <(firewall-cmd --permanent --list-rich-rules)
    firewall-cmd --reload
fi
if { ! command -v ufw >/dev/null 2>&1 || ! ufw status 2>/dev/null | grep -qw "active"; } && \
   { ! command -v firewall-cmd >/dev/null 2>&1 || ! systemctl is-active --quiet firewalld 2>/dev/null; }; then
    echo "[ERROR] No active UFW/firewalld policy can restrict Veyon to teacher endpoints. The service will remain stopped." >&2
    exit 1
fi

echo "[5/6] Enabling the packaged Veyon service..."
cleanup_runtime_masks
systemctl daemon-reload
VY_SERVICE=""
for svc in veyon-service veyon; do
    if systemctl list-unit-files 2>/dev/null | grep -q "^$svc.service"; then
        VY_SERVICE="$svc"
        break
    fi
done
if [ -z "$VY_SERVICE" ]; then
    echo "[ERROR] No packaged Veyon systemd service was found."
    exit 1
fi
systemctl enable "$VY_SERVICE.service"
systemctl restart "$VY_SERVICE.service"

# Retire the old wrapper if an earlier installer created it. It only restarted
# Veyon redundantly and could defeat intentional service stops.
if [ -f /etc/systemd/system/synapse-agent.service ]; then
    systemctl disable --now synapse-agent.service 2>/dev/null || true
    rm -f /etc/systemd/system/synapse-agent.service
    systemctl daemon-reload
fi

if [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    if ! command -v pipewire >/dev/null 2>&1 || ! command -v xdg-desktop-portal >/dev/null 2>&1; then
        echo "[ERROR] Wayland detected but PipeWire or xdg-desktop-portal is missing. Install both before using screen monitoring."
        exit 1
    fi
    echo "[INFO] Wayland detected. The desktop portal may request permission for screen capture."
fi
if command -v synapse-linux-check >/dev/null 2>&1; then
    if ! SYNAPSE_REQUIRE_TAILSCALE=0 synapse-linux-check; then
        echo "[ERROR] Readiness checks failed. Resolve the reported issues before using this computer." >&2
        exit 1
    fi
else
    echo "[ERROR] Readiness checker was not installed. Restore Tools/synapse-linux-check.sh and rerun this installer." >&2
    exit 1
fi

echo "[6/6] Basic Linux client readiness checks passed."

echo "=========================================================="
echo " [OK] Synapse Linux Lab Client passed basic readiness checks."
echo " Confirm live screen capture/control from the teacher console."
echo "=========================================================="
SETUP_COMPLETE=1
