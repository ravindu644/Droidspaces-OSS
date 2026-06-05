#!/bin/bash

# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

# Reject if not running inside Termux
if [ -z "$TERMUX_APP__PACKAGE_NAME" ] || [ -z "$PREFIX" ]; then
    echo "Error: not running inside Termux." >&2
    exit 1
fi

DEFAULT_PA="$PREFIX/etc/pulse/default.pa"
AAUDIO_LINE="load-module module-aaudio-sink"
ALWAYS_LINE="load-module module-always-sink"
SLES_LINE="load-module module-sles-sink"
CK_LINE="load-module module-console-kit"

BOLD="\033[1m"
GREEN="\033[1;32m"
CYAN="\033[0;36m"
RESET="\033[0m"

log() {
    echo -e "${GREEN}===> ${BOLD}$*${RESET}\n"
}

detail() {
    echo -e "  ${CYAN}->${RESET} $*"
}

log "Setting up Droidspaces dependencies..."

log "Updating repos and upgrading Termux..."
DEBIAN_FRONTEND=noninteractive pkg update -y -o Dpkg::Options::="--force-confold"
DEBIAN_FRONTEND=noninteractive pkg upgrade -y -o Dpkg::Options::="--force-confold"

log "Installing x11-repo..."
DEBIAN_FRONTEND=noninteractive pkg install -y -o Dpkg::Options::="--force-confold" x11-repo

log "Installing Termux:X11, VirGL and PulseAudio..."
DEBIAN_FRONTEND=noninteractive pkg install -y -o Dpkg::Options::="--force-confold" pulseaudio termux-x11 virglrenderer-android

log "Patching $DEFAULT_PA..."

if [ ! -f "$DEFAULT_PA" ]; then
    echo "Error: $DEFAULT_PA not found." >&2
    exit 1
fi

# Comment out module-sles-sink (missing Android HAL deps on most devices)
if grep -q "^${SLES_LINE}" "$DEFAULT_PA"; then
    sed -i "s|^${SLES_LINE}|#${SLES_LINE}|" "$DEFAULT_PA"
    detail "Commented out $SLES_LINE"
fi

# Comment out module-console-kit (no D-Bus system bus on Android, causes futex deadlock)
if grep -q "^${CK_LINE}" "$DEFAULT_PA"; then
    sed -i "s|^${CK_LINE}|#${CK_LINE}|" "$DEFAULT_PA"
    detail "Commented out $CK_LINE"
fi

# Check if already patched (aaudio appears before always-sink)
AAUDIO_LINE_NUM=$(grep -n "^${AAUDIO_LINE}$" "$DEFAULT_PA" | head -1 | cut -d: -f1)
ALWAYS_LINE_NUM=$(grep -n "^${ALWAYS_LINE}$" "$DEFAULT_PA" | head -1 | cut -d: -f1)

if [ -n "$AAUDIO_LINE_NUM" ] && [ -n "$ALWAYS_LINE_NUM" ] && [ "$AAUDIO_LINE_NUM" -lt "$ALWAYS_LINE_NUM" ]; then
    detail "default.pa already patched, skipping."
else
    sed -i "s|^${ALWAYS_LINE}|${AAUDIO_LINE}\n${ALWAYS_LINE}|" "$DEFAULT_PA"
    detail "Injected $AAUDIO_LINE before $ALWAYS_LINE"

    # Remove duplicate aaudio line at bottom if present
    AAUDIO_COUNT=$(grep -c "^${AAUDIO_LINE}$" "$DEFAULT_PA" || true)
    if [ "$AAUDIO_COUNT" -gt 1 ]; then
        awk "BEGIN{found=0} /^${AAUDIO_LINE}$/{if(found){next}; found=1} {print}" "$DEFAULT_PA" > "$DEFAULT_PA.tmp"
        mv "$DEFAULT_PA.tmp" "$DEFAULT_PA"
        detail "Removed duplicate $AAUDIO_LINE"
    fi
fi

log "All done. Droidspaces audio/display dependencies are ready."
