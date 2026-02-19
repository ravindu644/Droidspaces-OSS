#!/bin/bash
# install-musl.sh — Cross-compilation toolchain installer for Droidspaces
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

set -e

# Target Mappings
declare -A TARGETS
TARGETS["x86"]="i686-linux-musl"
TARGETS["x86_64"]="x86_64-linux-musl"
TARGETS["aarch64"]="aarch64-linux-musl"
TARGETS["armhf"]="arm-linux-musleabihf"

usage() {
    echo "Usage: $0 <arch>"
    echo ""
    echo "Architectures:"
    echo "  x86       (i686-linux-musl)"
    echo "  x86_64    (x86_64-linux-musl)"
    echo "  aarch64   (aarch64-linux-musl)"
    echo "  armhf     (arm-linux-musleabihf)"
    echo ""
    echo "Example: $0 aarch64"
    exit 1
}

if [ "$#" -ne 1 ]; then
    usage
fi

ARCH=$1
TARGET=${TARGETS[$ARCH]}

if [ -z "$TARGET" ]; then
    echo "Error: Unknown architecture '$ARCH'"
    usage
fi

echo "[*] Preparing to install musl toolchain for $ARCH ($TARGET)..."

# 1. Dependency Check
echo "[*] Checking build dependencies..."
MISSING_DEPS=()
# Basic tools required for cloning and building
for cmd in gcc g++ make git wget patch bzip2 xz; do
    if ! command -v $cmd >/dev/null 2>&1; then
        MISSING_DEPS+=($cmd)
    fi
done

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo "[-] Error: Missing required build tools: ${MISSING_DEPS[*]}"
    echo "[!] Please install the missing dependencies to continue."
    echo ""
    echo "Common installation commands:"
    echo "  - Debian/Ubuntu:  sudo apt install build-essential git wget patch bzip2 xz-utils"
    echo "  - Fedora/RHEL:    sudo dnf groupinstall \"Development Tools\" && sudo dnf install git wget patch"
    echo "  - Arch Linux:     sudo pacman -S base-devel git wget"
    echo "  - Alpine Linux:   apk add build-base git wget patch bzip2 xz"
    echo ""
    exit 1
fi

# 2. Setup Toolchain Directory
if [ -n "$SUDO_USER" ]; then
    HOME_DIR="$(eval echo ~$SUDO_USER)"
else
    HOME_DIR="$HOME"
fi

TOOLCHAIN_PARENT="$HOME_DIR/toolchains"
TOOLCHAIN_DIR="$TOOLCHAIN_PARENT/${TARGET}-cross"

mkdir -p "$TOOLCHAIN_PARENT"

# 3. Clone musl-cross-make
MUSL_CROSS_MAKE_DIR="/tmp/musl-cross-make"
if [ ! -d "$MUSL_CROSS_MAKE_DIR" ]; then
    echo "[*] Cloning musl-cross-make..."
    git clone --depth 1 https://github.com/richfelker/musl-cross-make.git "$MUSL_CROSS_MAKE_DIR"
fi

cd "$MUSL_CROSS_MAKE_DIR"

# 4. Build and Install
if [ -d "$TOOLCHAIN_DIR" ]; then
    echo "[+] $TARGET toolchain is already installed at $TOOLCHAIN_DIR"
    exit 0
fi

echo "[*] Building $TARGET toolchain (this may take a while)..."
make clean 2>/dev/null || true

# We use the default config, but specify the TARGET.
# IMPORTANT:
# 1. We override DL_CMD to include a User-Agent. GNU mirrors (ftpmirror) 
#    often 403 block plain wget requests.
# 2. We use OUTPUT instead of PREFIX. In musl-cross-make, 'make install'
#    installs into the directory specified by OUTPUT.
make TARGET=$TARGET \
     DL_CMD='wget -c --user-agent="Mozilla/5.0" -O' \
     -j$(nproc)

make install TARGET=$TARGET \
     OUTPUT="$TOOLCHAIN_DIR"

# Verify installation
if [ -d "$TOOLCHAIN_DIR/bin" ]; then
    echo "[+] Successfully installed $TARGET toolchain to $TOOLCHAIN_DIR"
    echo ""
    echo "To use it, ensure your Makefile points to:"
    echo "  $TOOLCHAIN_DIR/bin/${TARGET}-gcc"
else
    # Check if it was installed to a differently named output dir (e.g. output-x86_64-linux-musl)
    # This happens if NATIVE=1 is set or if HOST is detected.
    DEBUG_OUTPUT=$(ls -d /tmp/musl-cross-make/output* 2>/dev/null | head -n 1)
    if [ -n "$DEBUG_OUTPUT" ] && [ -d "$DEBUG_OUTPUT/bin" ]; then
        mv "$DEBUG_OUTPUT" "$TOOLCHAIN_DIR"
        echo "[+] Successfully installed $TARGET toolchain to $TOOLCHAIN_DIR"
    else
        echo "[-] Error: Build failed or installation directory not created."
        exit 1
    fi
fi
