#!/bin/bash
# Launcher for Synapse Command Web Console in standalone native desktop app mode on Linux

URL="http://127.0.0.1:8080"
LOCAL_HTML="/opt/synapse-agent/Synapse Command Redesign.html"

TARGET_URL="$URL"
if [ ! -z "$1" ]; then
    TARGET_URL="$1"
fi

# 1. Try Google Chrome App Mode
if command -v google-chrome &> /dev/null; then
    exec google-chrome --app="$TARGET_URL" --disable-extensions --class=SynapseCommand
fi

# 2. Try Chromium App Mode
if command -v chromium &> /dev/null; then
    exec chromium --app="$TARGET_URL" --disable-extensions --class=SynapseCommand
fi

# 3. Try Chromium-browser (Ubuntu)
if command -v chromium-browser &> /dev/null; then
    exec chromium-browser --app="$TARGET_URL" --disable-extensions --class=SynapseCommand
fi

# 4. Fallback to default browser
if command -v xdg-open &> /dev/null; then
    exec xdg-open "$TARGET_URL"
fi

echo "Could not find a supported browser to launch Synapse Command."
exit 1
