#!/usr/bin/env bash
# ==============================================================================
# Synapse Command - Linux BYOD Student Laptop Installer v4.11.9
# Self-contained installer for student laptops connecting to the classroom fleet.
# Compatible with: Ubuntu, Debian, Linux Mint, Pop!_OS, Fedora, Arch, openSUSE
# ==============================================================================
set -e

MIN_VEYON_VERSION="4.11.5"
TEACHER_LAN_IP="${SYNAPSE_TEACHER_LAN_IP:-192.168.1.150}"
TEACHER_TAILSCALE_IP="${SYNAPSE_TEACHER_TAILSCALE_IP:-100.99.161.43}"

echo "=========================================================="
echo "    SYNAPSE COMMAND — LINUX BYOD CLIENT INSTALLER        "
echo "=========================================================="

if [ "$EUID" -ne 0 ]; then
    echo "[!] Requesting root privileges..."
    if [[ -v SYNAPSE_TAILSCALE_AUTHKEY || -v SYNAPSE_TEACHER_LAN_IP || -v SYNAPSE_TEACHER_TAILSCALE_IP ]]; then
        exec sudo --preserve-env=SYNAPSE_TAILSCALE_AUTHKEY,SYNAPSE_TEACHER_LAN_IP,SYNAPSE_TEACHER_TAILSCALE_IP bash "$0" "$@"
    fi
    exec sudo bash "$0" "$@"
fi

echo ""
echo "MONITORING CONSENT — LINUX BYOD ENROLLMENT"
echo "This laptop will connect to the college Tailscale network and Veyon classroom system."
echo "While enrolled, authorized staff can view the live screen, take screenshots, control or lock the session, send messages, and use supported app and file-transfer features."
echo "The installer records the accepting Linux account and time in /var/lib/synapse/byod-consent.json."
echo "Release periods are controlled by the instructor; the Veyon service is stopped while released."
echo ""
if [ ! -r /dev/tty ]; then
    echo "[ERROR] BYOD consent requires an interactive terminal. No system changes were made." >&2
    exit 2
fi
printf 'Type YES to accept monitoring and continue enrollment: ' >&2
IFS= read -r CONSENT_RESPONSE </dev/tty
if [ "$CONSENT_RESPONSE" != "YES" ]; then
    echo "[CANCELLED] Consent was not accepted. No system changes were made."
    exit 2
fi
CONSENT_ACCEPTED_BY="${SUDO_USER:-$(id -un)}"
CONSENT_ACCEPTED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# Some package post-install scripts start Veyon immediately. Keep both known
# service names runtime-masked until the teacher-only firewall policy is in.
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

echo "[1/7] Detecting Linux distribution and installing client..."
if command -v apt-get &>/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq veyon-service veyon-plugins libfakekey0 curl jq python3 || apt-get install -y -qq veyon-service curl python3
elif command -v dnf &>/dev/null; then
    dnf install -y -q veyon veyon-service curl jq python3 || dnf install -y -q veyon curl python3
elif command -v pacman &>/dev/null; then
    pacman -S --needed --noconfirm veyon curl python
elif command -v zypper &>/dev/null; then
    zypper in -y veyon curl python3
else
    echo "[!] Unknown package manager. Please ensure 'veyon-service' is installed."
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

echo "[2/7] Installing and joining the college Tailscale network..."
if ! command -v tailscale >/dev/null 2>&1; then
    if ! command -v curl >/dev/null 2>&1; then
        echo "[ERROR] curl is required to install Tailscale from its official Linux installer."
        exit 1
    fi
    curl -fsSL https://tailscale.com/install.sh | sh
fi
if ! command -v tailscale >/dev/null 2>&1; then
    echo "[ERROR] Tailscale installation did not provide the tailscale command."
    exit 1
fi
systemctl enable --now tailscaled
TAILSCALE_STATE=$(tailscale status --json 2>/dev/null | python3 -c 'import json,sys; print(json.load(sys.stdin).get("BackendState", ""))' 2>/dev/null || true)
if [ "$TAILSCALE_STATE" != "Running" ]; then
    if [ -n "${SYNAPSE_TAILSCALE_AUTHKEY:-}" ]; then
        AUTHKEY_FILE=$(mktemp)
        trap 'rm -f "$AUTHKEY_FILE"' EXIT
        chmod 600 "$AUTHKEY_FILE"
        printf '%s' "$SYNAPSE_TAILSCALE_AUTHKEY" > "$AUTHKEY_FILE"
        tailscale up --auth-key="file:$AUTHKEY_FILE"
        rm -f "$AUTHKEY_FILE"
        trap - EXIT
        unset SYNAPSE_TAILSCALE_AUTHKEY
    else
        echo "[INFO] Authenticate this device to the college tailnet using the URL shown below."
        tailscale up
    fi
fi
TAILSCALE_STATE=$(tailscale status --json 2>/dev/null | python3 -c 'import json,sys; print(json.load(sys.stdin).get("BackendState", ""))' 2>/dev/null || true)
if [ "$TAILSCALE_STATE" != "Running" ]; then
    echo "[ERROR] This device is not authenticated to Tailscale. Complete tailnet enrollment, then rerun this installer."
    exit 1
fi

echo "[3/7] Configuring Security Public Key (Digihub Classroom)..."
mkdir -p /etc/veyon/keys/public/Digihub
chmod 755 /etc/veyon/keys /etc/veyon/keys/public /etc/veyon/keys/public/Digihub
install -d -m 0755 /etc/synapse
cat > /etc/synapse/teacher-endpoints.conf <<EOF
TEACHER_LAN_IP=$TEACHER_LAN_IP
TEACHER_TAILSCALE_IP=$TEACHER_TAILSCALE_IP
EOF
chmod 0644 /etc/synapse/teacher-endpoints.conf

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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK_SCRIPT="$SCRIPT_DIR/../Tools/synapse-linux-check.sh"
if [ ! -f "$CHECK_SCRIPT" ]; then CHECK_SCRIPT="$SCRIPT_DIR/synapse-linux-check.sh"; fi
if [ -f "$CHECK_SCRIPT" ]; then
    install -m 0755 "$CHECK_SCRIPT" /usr/local/bin/synapse-linux-check
fi

echo "[4/7] Setting Authentication & Service Configuration..."
if command -v veyon-cli &>/dev/null; then
    veyon-cli config set Authentication/Method 1
    veyon-cli config set Authentication/KeyFileDirectories/Public "/etc/veyon/keys/public"
    veyon-cli config set Service/Autostart true
fi

echo "[5/7] Restricting Veyon access to teacher endpoints..."
if command -v ufw &>/dev/null && ufw status 2>/dev/null | grep -qw "active"; then
    # Replace only rules owned by this installer so changed teacher endpoints
    # cannot leave stale allow rules behind and repeated installs stay bounded.
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
if command -v firewall-cmd &>/dev/null && systemctl is-active --quiet firewalld 2>/dev/null; then
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

echo "[6/7] Starting Synapse Service..."
cleanup_runtime_masks
systemctl daemon-reload
RELEASE_MARKER="/var/lib/synapse/monitoring-released"
service_found=0
for svc in veyon-service veyon; do
    if systemctl list-unit-files "${svc}.service" --no-legend 2>/dev/null | grep -q "^${svc}.service"; then
        service_found=1
        if [ -f "$RELEASE_MARKER" ]; then
            echo "[INFO] Persisted release found; ensuring $svc.service remains disabled and stopped."
            systemctl disable --now "$svc.service"
            if systemctl is-active --quiet "$svc.service"; then
                echo "[ERROR] Monitoring is released, but $svc.service is still active." >&2
                exit 1
            fi
        else
            systemctl enable "$svc.service"
            systemctl restart "$svc.service"
            if ! systemctl is-active --quiet "$svc.service"; then
                echo "[ERROR] $svc.service failed to become active." >&2
                exit 1
            fi
        fi
        break
    fi
done
if [ "$service_found" -eq 0 ]; then
    echo "[ERROR] No packaged Veyon systemd service was found." >&2
    exit 1
fi

echo "[7/7] Checking display and client readiness..."
if command -v synapse-linux-check >/dev/null 2>&1; then
    if ! synapse-linux-check; then
        echo "[ERROR] Readiness checks failed. The client is not ready for enrollment; resolve the reported issues and rerun this installer." >&2
        exit 1
    fi
else
    echo "[ERROR] Readiness checker was not installed. Restore Tools/synapse-linux-check.sh and rerun this installer." >&2
    exit 1
fi

# Write the consent record only after required client/readiness checks succeed.
install -d -m 0755 /var/lib/synapse
LOCAL_HOST=$(hostname 2>/dev/null || printf 'unknown')
CONSENT_TMP=$(mktemp /var/lib/synapse/.byod-consent.XXXXXX)
python3 - "$CONSENT_TMP" "$CONSENT_ACCEPTED_BY" "$CONSENT_ACCEPTED_AT" "$LOCAL_HOST" <<'PY'
import json, os, sys
path, accepted_by, accepted_at, hostname = sys.argv[1:5]
record = {
    "version": 1,
    "accepted_at_utc": accepted_at,
    "accepted_by": accepted_by,
    "device_hostname": hostname,
    "consent_scope": ["Tailscale enrollment", "live screen viewing", "screenshots", "remote control", "session locking", "messages", "supported app and file transfer controls"],
    "monitoring_state_at_install": "released" if os.path.exists("/var/lib/synapse/monitoring-released") else "active",
}
with open(path, "w", encoding="utf-8") as output:
    json.dump(record, output, indent=2)
    output.write("\n")
PY
chmod 0644 "$CONSENT_TMP"
mv -f "$CONSENT_TMP" /var/lib/synapse/byod-consent.json

# Fetch Local IP and Hostname
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')

echo "[*] Announcing presence to Synapse Command..."
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

echo ""
echo "=========================================================="
echo " [SUCCESS] Synapse BYOD Client passed basic readiness checks."
echo "=========================================================="
echo " Device Information:"
echo "   Hostname : ${LOCAL_HOST}"
echo "   IP       : ${LOCAL_IP}"
echo "=========================================================="
echo " Add this computer in Synapse Command and confirm live screen"
echo " capture/control before treating it as enrolled."
echo "=========================================================="
SETUP_COMPLETE=1
