#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
chmod +x "$SCRIPT_DIR/install-synapse-byod.sh"
sudo "$SCRIPT_DIR/install-synapse-byod.sh"
