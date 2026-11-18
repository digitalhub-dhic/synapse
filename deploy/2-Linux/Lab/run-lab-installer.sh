#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
chmod +x "$SCRIPT_DIR/install-synapse-lab.sh"
sudo "$SCRIPT_DIR/install-synapse-lab.sh"
