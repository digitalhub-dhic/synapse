#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
chmod +x "$SCRIPT_DIR/install-synapse-byod.sh"
if [[ -v SYNAPSE_TAILSCALE_AUTHKEY || -v SYNAPSE_TEACHER_LAN_IP || -v SYNAPSE_TEACHER_TAILSCALE_IP ]]; then
    sudo --preserve-env=SYNAPSE_TAILSCALE_AUTHKEY,SYNAPSE_TEACHER_LAN_IP,SYNAPSE_TEACHER_TAILSCALE_IP "$SCRIPT_DIR/install-synapse-byod.sh"
else
    sudo "$SCRIPT_DIR/install-synapse-byod.sh"
fi
