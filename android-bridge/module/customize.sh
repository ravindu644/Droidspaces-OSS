# android-bridge installer, sourced by the KernelSU installer
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

case "$ARCH" in
    arm64) abi=aarch64 ;;
    arm) abi=armhf ;;
    x64) abi=x86_64 ;;
    x86) abi=x86 ;;
    riscv64) abi=riscv64 ;;
    *) abort "! Unsupported architecture: $ARCH" ;;
esac

[ -f "$MODPATH/bin/$abi/android-bridge" ] || abort "! No android-bridge binary for $abi"
mv "$MODPATH/bin/$abi/android-bridge" "$MODPATH/android-bridge"
rm -rf "$MODPATH/bin"
set_perm "$MODPATH/android-bridge" 0 0 0755

ui_print "- Installed android-bridge for $abi"
ui_print "- The socket appears at /dev/android-bridge/bridge.sock after a reboot"
