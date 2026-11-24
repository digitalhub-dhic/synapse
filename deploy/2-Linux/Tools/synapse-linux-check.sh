#!/usr/bin/env bash
# Synapse Linux client readiness check for X11 and Wayland sessions.
set -u

MIN_VERSION="4.11.5"
KEY_FILE="/etc/veyon/keys/public/Digihub/key"
RELEASE_MARKER="/var/lib/synapse/monitoring-released"
TEACHER_LAN_IP="192.168.1.150"
TEACHER_TAILSCALE_IP="100.99.161.43"
REQUIRE_TAILSCALE="${SYNAPSE_REQUIRE_TAILSCALE:-1}"
if [ -r /etc/synapse/teacher-endpoints.conf ]; then
    # File is root-managed by the installer and contains validated IPv4 values.
    . /etc/synapse/teacher-endpoints.conf
fi
failed=0

report() { printf '[%s] %s\n' "$1" "$2"; }

port_spec_contains_veyon_port() {
    local specification="$1" low high port
    specification="${specification%/tcp}"
    if [[ "$specification" =~ ^([0-9]+)-([0-9]+)$ ]]; then
        low=${BASH_REMATCH[1]}; high=${BASH_REMATCH[2]}
        for port in 11100 11400; do
            if (( low <= port && port <= high )); then return 0; fi
        done
    elif [[ "$specification" =~ ^[0-9]+$ ]] && { [ "$specification" = 11100 ] || [ "$specification" = 11400 ]; }; then
        return 0
    fi
    return 1
}

if ! python3 - "$TEACHER_LAN_IP" "$TEACHER_TAILSCALE_IP" <<'PY' >/dev/null 2>&1
import ipaddress, sys
for value in sys.argv[1:]:
    ipaddress.IPv4Address(value)
PY
then
    report FAIL "Teacher endpoint configuration contains an invalid IPv4 address"
    exit 1
fi

if ! command -v veyon-cli >/dev/null 2>&1; then
    report FAIL "veyon-cli is missing"
    failed=1
else
    version=$(veyon-cli --version 2>&1 | sed -nE 's/.*([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' | head -n 1)
    if [ -z "$version" ] || [ "$(printf '%s\n' "$MIN_VERSION" "$version" | sort -V | head -n 1)" != "$MIN_VERSION" ]; then
        report FAIL "Veyon ${MIN_VERSION}+ required; detected ${version:-unknown}"
        failed=1
    else
        report OK "Veyon $version"
    fi
fi

if [ -s "$KEY_FILE" ]; then report OK "Classroom public key installed"; else report FAIL "Classroom public key missing at $KEY_FILE"; failed=1; fi

# A successful enrollment check requires a host firewall policy that limits
# Veyon to the configured teacher endpoints. A warning alone allowed installs
# to report ready while the Veyon service was exposed to every reachable host.
firewall_ready=0
if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -qw "active"; then
    ufw_rules=$(ufw status 2>/dev/null || true)
    unexpected_veyon_allow=0
    while IFS= read -r rule; do
        case "$rule" in
            *ALLOW*)
                ufw_port=$(awk '{print $1}' <<<"$rule")
                if ! port_spec_contains_veyon_port "$ufw_port"; then continue; fi
                case "$rule" in
                    *"${TEACHER_LAN_IP}"*"Synapse Teacher LAN"*|*"${TEACHER_TAILSCALE_IP}"*"Synapse Teacher Tailscale"*) ;;
                    *) unexpected_veyon_allow=1 ;;
                esac
                ;;
        esac
    done <<<"$ufw_rules"
    if grep -Fq "Synapse Teacher LAN 11100" <<<"$ufw_rules" && \
       grep -Fq "$TEACHER_LAN_IP" <<<"$ufw_rules" && \
       grep -Fq "Synapse Teacher Tailscale 11100" <<<"$ufw_rules" && \
       grep -Fq "$TEACHER_TAILSCALE_IP" <<<"$ufw_rules" && \
       grep -Fq "Synapse Teacher LAN 11400" <<<"$ufw_rules" && \
       grep -Fq "Synapse Teacher Tailscale 11400" <<<"$ufw_rules" && \
       grep -Fq "Restrict Synapse 11100 to teacher endpoints" <<<"$ufw_rules" && \
       grep -Fq "Restrict Synapse 11400 to teacher endpoints" <<<"$ufw_rules" && \
       [ "$unexpected_veyon_allow" -eq 0 ]; then
        report OK "UFW restricts Veyon access to teacher endpoints"
        firewall_ready=1
    fi
elif command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active --quiet firewalld 2>/dev/null; then
    lan_rule="rule family=ipv4 source address=${TEACHER_LAN_IP}/32 port port=11100 protocol=tcp accept"
    tail_rule="rule family=ipv4 source address=${TEACHER_TAILSCALE_IP}/32 port port=11100 protocol=tcp accept"
    lan_demo_rule="rule family=ipv4 source address=${TEACHER_LAN_IP}/32 port port=11400 protocol=tcp accept"
    tail_demo_rule="rule family=ipv4 source address=${TEACHER_TAILSCALE_IP}/32 port port=11400 protocol=tcp accept"
    rich_rules=$(firewall-cmd --permanent --list-rich-rules 2>/dev/null || true)
    unexpected_veyon_accept=0
    while IFS= read -r rule; do
        [[ "$rule" == *"protocol=tcp"*"accept"* ]] || continue
        rich_port=$(sed -n 's/.*port port=\([^ ]*\).*/\1/p' <<<"$rule")
        port_spec_contains_veyon_port "$rich_port" || continue
        if [ "$rule" != "$lan_rule" ] && [ "$rule" != "$tail_rule" ] && \
           [ "$rule" != "$lan_demo_rule" ] && [ "$rule" != "$tail_demo_rule" ]; then
            unexpected_veyon_accept=1
        fi
    done <<<"$rich_rules"
    if firewall-cmd --permanent --query-rich-rule="$lan_rule" >/dev/null 2>&1 && \
       firewall-cmd --permanent --query-rich-rule="$tail_rule" >/dev/null 2>&1 && \
       firewall-cmd --permanent --query-rich-rule="$lan_demo_rule" >/dev/null 2>&1 && \
       firewall-cmd --permanent --query-rich-rule="$tail_demo_rule" >/dev/null 2>&1 && \
       ! firewall-cmd --permanent --query-port=11100/tcp >/dev/null 2>&1 && \
       ! firewall-cmd --permanent --query-port=11400/tcp >/dev/null 2>&1 && \
       ! firewall-cmd --permanent --query-service=veyon >/dev/null 2>&1 && \
       [ "$unexpected_veyon_accept" -eq 0 ]; then
        report OK "firewalld restricts Veyon access to teacher endpoints"
        firewall_ready=1
    fi
fi
if [ "$firewall_ready" -eq 0 ]; then
    report FAIL "No verified UFW/firewalld rule set restricts Veyon to teacher endpoints"
    failed=1
fi

if [ "$REQUIRE_TAILSCALE" = "0" ]; then
    report INFO "Tailscale is not required for this client check"
elif ! command -v tailscale >/dev/null 2>&1; then
    report FAIL "Tailscale client is missing"
    failed=1
else
    ts_state=$(tailscale status --json 2>/dev/null | python3 -c 'import json,sys; print(json.load(sys.stdin).get("BackendState", ""))' 2>/dev/null || true)
    if [ "$ts_state" = "Running" ]; then
        report OK "Tailscale connected ($(tailscale ip -4 2>/dev/null | head -n 1))"
    else
        report FAIL "Tailscale is installed but not authenticated/connected"
        failed=1
    fi
fi

service_found=0
for svc in veyon-service veyon; do
    if systemctl list-unit-files "${svc}.service" --no-legend 2>/dev/null | grep -q "${svc}.service"; then
        service_found=1
        if [ -f "$RELEASE_MARKER" ]; then
            report INFO "Monitoring is released; $svc is intentionally not required to run"
        elif systemctl is-active --quiet "$svc.service"; then
            report OK "$svc.service active"
        else
            report FAIL "$svc.service is installed but inactive"
            failed=1
        fi
        break
    fi
done
if [ "$service_found" -eq 0 ]; then report FAIL "Veyon system service not found"; failed=1; fi

session_type=${XDG_SESSION_TYPE:-unknown}
case "$session_type" in
    wayland)
        command -v pipewire >/dev/null 2>&1 && report OK "PipeWire executable present" || { report FAIL "PipeWire executable missing"; failed=1; }
        command -v xdg-desktop-portal >/dev/null 2>&1 && report OK "Desktop portal executable present" || { report FAIL "xdg-desktop-portal missing"; failed=1; }
        if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ] && command -v systemctl >/dev/null 2>&1 && systemctl --user is-active --quiet pipewire.service; then
            report OK "PipeWire active for this user session"
        else
            report WARN "PipeWire session status cannot be confirmed; run this check from the student's graphical session"
        fi
        ;;
    x11) report OK "X11 session detected" ;;
    *) report WARN "No graphical session detected; rerun inside the student's logged-in desktop" ;;
esac

teacher_reachable=0
for teacher in "$TEACHER_LAN_IP" "$TEACHER_TAILSCALE_IP"; do
    if command -v timeout >/dev/null 2>&1 && timeout 2 bash -c ":</dev/tcp/$teacher/11100" 2>/dev/null; then
        report OK "Teacher server reachable at $teacher:11100"
        teacher_reachable=1
        break
    fi
done
if [ "$teacher_reachable" -eq 0 ]; then
    report WARN "Teacher server is unreachable on LAN and Tailscale (check tailnet ACLs and addresses)"
fi

if [ "$failed" -eq 0 ]; then
    report OK "Basic client readiness checks passed; confirm live capture/control from the teacher console"
    exit 0
fi
report FAIL "Resolve the failures above before enrolling this device"
exit 1
