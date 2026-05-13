#!/bin/bash
# install-musl.sh - Cross-compilation toolchain installer for Droidspaces
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

set -e

# Target Mappings
declare -A TARGETS
TARGETS["x86"]="i686-linux-musl"
TARGETS["x86_64"]="x86_64-linux-musl"
TARGETS["aarch64"]="aarch64-linux-musl"
TARGETS["armhf"]="arm-linux-musleabihf"
TARGETS["riscv64"]="riscv64-linux-musl"

usage() {
    echo "Usage: $0 <arch>"
    echo ""
    echo "Architectures:"
    echo "  x86       (i686-linux-musl)"
    echo "  x86_64    (x86_64-linux-musl)"
    echo "  aarch64   (aarch64-linux-musl)"
    echo "  armhf     (arm-linux-musleabihf)"
    echo "  riscv64   (riscv64-linux-musl)"
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
for cmd in gcc g++ make git wget curl patch bzip2 xz; do
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
MUSL_CROSS_MAKE_DIR="$HOME_DIR/musl-cross-make"
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
     GCC_VER=14.2.0 \
     BINUTILS_VER=2.44 \
     GNU_SITE=https://ftp.gnu.org/gnu \
     DL_CMD='curl -L -C - -A "Mozilla/5.0" -o' \
     -j$(nproc)

make install TARGET=$TARGET \
     OUTPUT="$TOOLCHAIN_DIR"

# 5. Post-install: copy Linux kernel headers if musl-cross-make skipped them.
#
# The sabotage linux-headers-4.19.88-2 package lacks an arch/riscv entry, so
# musl-cross-make's LINUX_ARCH detection returns empty for riscv64 and silently
# skips install-kernel-headers. We detect this and fix it manually.
SYSROOT_INCLUDE="$TOOLCHAIN_DIR/$TARGET/include"
if [ -d "$SYSROOT_INCLUDE" ] && [ ! -d "$SYSROOT_INCLUDE/linux" ]; then
    # Find the extracted linux-headers source (musl-cross-make puts it here)
    LINUX_HDR_SRC=$(find "$MUSL_CROSS_MAKE_DIR" -maxdepth 1 -type d -name "linux-headers-*" ! -name "*.orig" | head -n 1)
    if [ -z "$LINUX_HDR_SRC" ]; then
        LINUX_HDR_SRC=$(find "$MUSL_CROSS_MAKE_DIR" -maxdepth 1 -type d -name "linux-headers-*.orig" | head -n 1)
    fi

    if [ -n "$LINUX_HDR_SRC" ]; then
        echo "[*] Linux kernel headers missing from sysroot - installing manually..."
        # Generic headers: linux/, asm-generic/, drm/, scsi/, etc.
        if [ -d "$LINUX_HDR_SRC/generic/include" ]; then
            cp -rn "$LINUX_HDR_SRC/generic/include/." "$SYSROOT_INCLUDE/"
        fi
        # Arch-specific headers (asm/)
        TARGET_ARCH_NAME=$(echo "$TARGET" | cut -d'-' -f1)
        for arch_dir in "$LINUX_HDR_SRC/$TARGET_ARCH_NAME" "$LINUX_HDR_SRC/arch/$TARGET_ARCH_NAME"; do
            if [ -d "$arch_dir/include" ]; then
                cp -rn "$arch_dir/include/." "$SYSROOT_INCLUDE/"
                break
            fi
        done
        echo "[+] Linux kernel headers installed into sysroot."
    else
        echo "[!] Warning: Could not find linux-headers source to fix sysroot."
        echo "    Headers like <linux/loop.h> may be missing. Build may fail."
    fi
fi

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
