#!/usr/bin/env bash
# Compatibility entry point. The former custom daemon ran as root and exposed
# an unauthenticated x11vnc service. Linux deployments now use Veyon.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BYOD_INSTALLER="$SCRIPT_DIR/../deploy/2-Linux/BYOD/install-synapse-byod.sh"

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
    echo "Run this installer with sudo: sudo bash $0" >&2
    exit 1
fi
if [[ ! -f "$BYOD_INSTALLER" ]]; then
    echo "Synapse Veyon BYOD installer not found at: $BYOD_INSTALLER" >&2
    echo "Use the complete Synapse deployment package." >&2
    exit 1
fi

if systemctl list-unit-files synapse-agent.service --no-legend 2>/dev/null | grep -q '^synapse-agent\.service'; then
    echo "Stopping the retired root-level Synapse agent service..."
    systemctl disable --now synapse-agent.service || true
    rm -f /etc/systemd/system/synapse-agent.service
    systemctl daemon-reload
fi

echo "Installing the supported Veyon-based Linux BYOD client..."
exec bash "$BYOD_INSTALLER" "$@"
