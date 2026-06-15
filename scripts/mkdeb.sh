#!/bin/bash
# mkdeb.sh - Debian package builder for Droidspaces
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

BINARY_NAME="droidspaces"
OUT_DIR="output"
PKG_RELEASE="1"

usage() {
    echo "Usage: $0 [arch ...]"
    echo ""
    echo "Architectures:"
    echo "  x86_64    (Debian: amd64)"
    echo "  aarch64   (Debian: arm64)"
    echo "  armhf     (Debian: armhf)"
    echo "  x86       (Debian: i386)"
    echo "  riscv64   (Debian: riscv64)"
    echo ""
    echo "If no architecture is supplied, all built binaries in $OUT_DIR are packaged."
    exit 1
}

deb_arch_for() {
    case "$1" in
        x86_64)  echo "amd64" ;;
        aarch64) echo "arm64" ;;
        armhf)   echo "armhf" ;;
        x86)     echo "i386" ;;
        riscv64) echo "riscv64" ;;
        *)       return 1 ;;
    esac
}

detect_arch() {
    local desc
    desc=$(file "$1")

    case "$desc" in
        *x86-64*)                   echo "x86_64" ;;
        *ARM\ aarch64*)             echo "aarch64" ;;
        *RISC-V*)                   echo "riscv64" ;;
        *Intel\ 80386*|*80386*|*i386*) echo "x86" ;;
        *ARM*)                      echo "armhf" ;;
        *)                          return 1 ;;
    esac
}

package_one() {
    local arch="$1"
    local binary="$2"
    local deb_arch
    local pkg_version
    local pkg_name
    local temp_dir
    local pkg_root
    local installed_size

    deb_arch=$(deb_arch_for "$arch") || {
        echo "[-] Error: Unsupported architecture '$arch'"
        exit 1
    }

    if [ ! -f "$binary" ]; then
        echo "[-] Error: Binary not found: $binary"
        exit 1
    fi

    pkg_version="${VERSION}-${PKG_RELEASE}"
    pkg_name="${BINARY_NAME}_${pkg_version}_${deb_arch}.deb"
    temp_dir=$(mktemp -d "/tmp/${BINARY_NAME}-deb-${arch}.XXXXXX")
    pkg_root="$temp_dir/${BINARY_NAME}_${pkg_version}_${deb_arch}"

    mkdir -p "$pkg_root/DEBIAN"
    install -Dm755 "$binary" "$pkg_root/usr/bin/$BINARY_NAME"
    install -Dm644 "LICENSE" "$pkg_root/usr/share/doc/$BINARY_NAME/copyright"
    install -Dm644 "README.md" "$pkg_root/usr/share/doc/$BINARY_NAME/README.md"
    gzip -9n "$pkg_root/usr/share/doc/$BINARY_NAME/README.md"

    installed_size=$(du -sk "$pkg_root/usr" | awk '{print $1}')

    cat > "$pkg_root/DEBIAN/control" <<EOF_CONTROL
Package: droidspaces
Version: $pkg_version
Section: utils
Priority: optional
Architecture: $deb_arch
Maintainer: Droidspaces contributors <droidcasts@protonmail.com>
Installed-Size: $installed_size
Homepage: https://github.com/ravindu644/Droidspaces-OSS
Description: Lightweight Linux container runtime for Android and Linux
 Droidspaces is a static, namespace-based container runtime with support for
 full init systems such as systemd, OpenRC, runit, s6, and SysV init.
EOF_CONTROL

    chmod 0755 "$pkg_root/DEBIAN"
    chmod 0644 "$pkg_root/DEBIAN/control"

    echo "[*] Creating $pkg_name..."
    dpkg-deb --build --root-owner-group "$pkg_root" "$pkg_name" >/dev/null
    rm -rf "$temp_dir"
    echo "[+] Created: $pkg_name ($(du -h "$pkg_name" | cut -f1))"
}

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "[-] Error: dpkg-deb not found. Please install dpkg."
    exit 1
fi

VERSION=$(grep "DS_VERSION" "src/include/droidspace.h" | awk '{print $3}' | tr -d '"')
if [ -z "$VERSION" ]; then
    echo "[-] Error: Could not determine Droidspaces version."
    exit 1
fi

ARCHES=()
if [ "$#" -gt 0 ]; then
    for arch in "$@"; do
        case "$arch" in
            -h|--help) usage ;;
            *) ARCHES+=("$arch") ;;
        esac
    done
else
    for arch in x86_64 aarch64 armhf x86 riscv64; do
        if [ -f "$OUT_DIR/$BINARY_NAME-$arch" ]; then
            ARCHES+=("$arch")
        fi
    done

    if [ "${#ARCHES[@]}" -eq 0 ] && [ -f "$OUT_DIR/$BINARY_NAME" ]; then
        ARCHES+=("$(detect_arch "$OUT_DIR/$BINARY_NAME")")
    fi
fi

if [ "${#ARCHES[@]}" -eq 0 ]; then
    echo "[-] Error: No built Droidspaces binaries found in $OUT_DIR."
    echo "[!] Build first with: make native or make all-build"
    exit 1
fi

for arch in "${ARCHES[@]}"; do
    if [ -f "$OUT_DIR/$BINARY_NAME-$arch" ]; then
        package_one "$arch" "$OUT_DIR/$BINARY_NAME-$arch"
    elif [ "${#ARCHES[@]}" -eq 1 ] && [ -f "$OUT_DIR/$BINARY_NAME" ]; then
        package_one "$arch" "$OUT_DIR/$BINARY_NAME"
    else
        echo "[-] Error: Binary not found: $OUT_DIR/$BINARY_NAME-$arch"
        exit 1
    fi
done
