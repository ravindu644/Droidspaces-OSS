#!/system/bin/sh
# Post-Extraction Fixes for Linux on Android
# Copyright (c) 2025 ravindu644
# Applies generic fixes after rootfs tarball extraction
# This script runs after extraction but before unmounting

set -e

# Parameters
ROOTFS_PATH="$1"
BUSYBOX_PATH="${BUSYBOX_PATH:-/data/local/Droidspaces/bin/busybox}"

# Logging function
log() { echo "[POST-FIX] $1"; }
warn() { echo "[POST-FIX-WARN] $1" >&2; }

# Check parameters
if [ -z "$ROOTFS_PATH" ]; then
    warn "Usage: $0 <rootfs_path>"
    exit 1
fi

# Check if rootfs path exists
if [ ! -d "$ROOTFS_PATH" ]; then
    warn "Rootfs path does not exist: $ROOTFS_PATH"
    exit 1
fi

log "Starting post-extraction fixes for: $ROOTFS_PATH"

# Check if systemd is available
SYSTEMD_SYSTEM_PATH=""
if [ -d "$ROOTFS_PATH/lib/systemd/system" ]; then
    SYSTEMD_SYSTEM_PATH="$ROOTFS_PATH/lib/systemd/system"
elif [ -d "$ROOTFS_PATH/usr/lib/systemd/system" ]; then
    SYSTEMD_SYSTEM_PATH="$ROOTFS_PATH/usr/lib/systemd/system"
else
    log "Systemd not found, skipping systemd-specific fixes"
    exit 0
fi

log "Systemd detected, applying fixes..."

# 1. Fix internet (DNS configuration)
log "Configuring DNS settings..."
mkdir -p "$ROOTFS_PATH/etc/systemd/resolved.conf.d"
cat > "$ROOTFS_PATH/etc/systemd/resolved.conf.d/dns.conf" << 'EOF'
[Resolve]
DNS=8.8.8.8 8.8.4.4
FallbackDNS=1.1.1.1
EOF

# 2. Enable systemd-resolved and systemd-networkd by default
log "Enabling systemd-resolved and systemd-networkd..."
mkdir -p "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants"

# Enable systemd-resolved
if [ -f "$SYSTEMD_SYSTEM_PATH/systemd-resolved.service" ]; then
    ln -sf "$SYSTEMD_SYSTEM_PATH/systemd-resolved.service" \
        "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/systemd-resolved.service"
fi

# Enable systemd-networkd
if [ -f "$SYSTEMD_SYSTEM_PATH/systemd-networkd.service" ]; then
    ln -sf "$SYSTEMD_SYSTEM_PATH/systemd-networkd.service" \
        "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/systemd-networkd.service"
fi

# Mask systemd-networkd-wait-online.service
log "Masking systemd-networkd-wait-online..."
ln -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-networkd-wait-online.service"

# Mask systemd-machine-id-commit.service
log "Masking systemd-machine-id-commit..."
ln -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-machine-id-commit.service"

# 3. Disable power button handling in systemd-logind to prevent container shutdown
log "Disabling power button handling in systemd-logind..."
mkdir -p "$ROOTFS_PATH/etc/systemd/logind.conf.d"
cat > "$ROOTFS_PATH/etc/systemd/logind.conf.d/99-disable-power-button.conf" << 'EOF'
[Login]
HandlePowerKey=ignore
HandlePowerKeyLongPress=ignore
HandlePowerKeyLongPressHibernate=ignore
EOF

# 4. Mask dangerous standard udev triggers (Prevents coldplugging Android hardware)
log "Masking dangerous udev triggers..."
mkdir -p "$ROOTFS_PATH/etc/systemd/system"
ln -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-udev-trigger.service"
ln -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-udev-settle.service"

# 5. Create a SAFE udev trigger service (Only triggers USB, Input, Block devices)
log "Creating safe udev trigger service..."
cat > "$ROOTFS_PATH/etc/systemd/system/safe-udev-trigger.service" << 'EOF'
[Unit]
Description=Safe Udev Trigger for Android
After=systemd-udevd-kernel.socket systemd-udevd-control.socket
Wants=systemd-udevd.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=-/usr/bin/udevadm trigger --subsystem-match=usb --subsystem-match=block --subsystem-match=input --subsystem-match=tty

[Install]
WantedBy=multi-user.target
EOF

# 6. Enable the safe trigger and ensure the main daemon is unmasked
log "Enabling safe udev trigger service..."
# Unmask systemd-udevd.service if it was masked
rm -f "$ROOTFS_PATH/etc/systemd/system/systemd-udevd.service"

# Enable the safe trigger service
if [ -d "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants" ]; then
    ln -sf /etc/systemd/system/safe-udev-trigger.service \
        "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/safe-udev-trigger.service"
fi

log "Post-extraction fixes completed successfully"
