#!/system/bin/sh
# Post-Extraction Fixes for Linux on Android
# Copyright (c) 2026 ravindu644
# Applies generic fixes after rootfs tarball extraction
# This script runs after extraction but before unmounting

set -e

# Parameters
ROOTFS_PATH="$1"
BUSYBOX_PATH="${BUSYBOX_PATH:-/data/local/Droidspaces/bin/busybox}"

# Use BusyBox applets for maximum compatibility
BB="$BUSYBOX_PATH"
ECHO="$BB echo"
MKDIR="$BB mkdir"
CAT="$BB cat"
GREP="$BB grep"
SED="$BB sed"
LN="$BB ln"
PRINTF="$BB printf"
RM="$BB rm"
TEST="$BB test"
CHMOD="$BB chmod"
CHROOT="$BB chroot"

# Logging function
log() { $ECHO "[POST-FIX] $1"; }
warn() { $ECHO "[POST-FIX-WARN] $1" >&2; }

# Check parameters
if $TEST -z "$ROOTFS_PATH"; then
    warn "Usage: $0 <rootfs_path>"
    exit 1
fi

# Check if rootfs path exists
if $TEST ! -d "$ROOTFS_PATH"; then
    warn "Rootfs path does not exist: $ROOTFS_PATH"
    exit 1
fi

log "Starting post-extraction fixes for: $ROOTFS_PATH"

# Check if fixes were already applied
if $TEST -f "$ROOTFS_PATH/etc/droidspaces"; then
    log "Post-extraction fixes already applied, skipping..."
    exit 0
fi

# Helper to execute a command inside the chroot environment
run_in_chroot() {
    local command="$*"
    local common_exports="export PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/libexec:/opt/bin'; export TMPDIR='/tmp';"
    
    # We use busybox chroot to run commands inside the rootfs
    # Note: This assumes /bin/sh exists in the rootfs
    $CHROOT "$ROOTFS_PATH" /bin/sh -c "$common_exports $command"
}

# --- 1. General Fixes (Init-independent) ---

# Android network group setup (required for socket access on Android kernels)
log "Setting up Android network groups..."
$GREP -q '^aid_inet:' "$ROOTFS_PATH/etc/group"    || $ECHO 'aid_inet:x:3003:'    >> "$ROOTFS_PATH/etc/group"
$GREP -q '^aid_net_raw:' "$ROOTFS_PATH/etc/group" || $ECHO 'aid_net_raw:x:3004:' >> "$ROOTFS_PATH/etc/group"
$GREP -q '^aid_net_admin:' "$ROOTFS_PATH/etc/group" || $ECHO 'aid_net_admin:x:3005:' >> "$ROOTFS_PATH/etc/group"

# Root gets required permissions for networking, input, and display
log "Granting root permissions for Android hardware access..."
run_in_chroot "usermod -a -G aid_inet,aid_net_raw,input,video,tty root 2>/dev/null || true"

# _apt needs aid_inet as primary group so apt works on Android
log "Fixing _apt user group for internet access..."
run_in_chroot "grep -q '^_apt:' /etc/passwd && usermod -g aid_inet _apt 2>/dev/null || true"

# Future users created with adduser automatically get network access
if $TEST -f "$ROOTFS_PATH/etc/adduser.conf"; then
    log "Configuring adduser for automatic Android group assignment..."
    $SED -i '/^EXTRA_GROUPS=/d; /^ADD_EXTRA_GROUPS=/d' "$ROOTFS_PATH/etc/adduser.conf"
    $ECHO 'ADD_EXTRA_GROUPS=1' >> "$ROOTFS_PATH/etc/adduser.conf"
    $ECHO 'EXTRA_GROUPS="aid_inet aid_net_raw input video tty"' >> "$ROOTFS_PATH/etc/adduser.conf"
fi

# --- 2. Systemd-Specific Fixes ---

# Check if systemd is available
GUEST_SYSTEMD_PATH=""
if $TEST -d "$ROOTFS_PATH/lib/systemd/system"; then
    GUEST_SYSTEMD_PATH="/lib/systemd/system"
elif $TEST -d "$ROOTFS_PATH/usr/lib/systemd/system"; then
    GUEST_SYSTEMD_PATH="/usr/lib/systemd/system"
else
    log "Systemd not found, skipping systemd-specific fixes"
    log "Post-extraction fixes completed successfully"
    exit 0
fi

log "Systemd detected (at $GUEST_SYSTEMD_PATH), applying fixes..."

# Mask problematic services for Android kernels
log "Masking problematic systemd services..."
# Mask systemd-networkd-wait-online.service
$LN -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-networkd-wait-online.service"
# Mask systemd-journald-audit.socket to prevent deadlocks on Android kernels
$LN -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-journald-audit.socket"

# Journald configuration (skip Audit, KMsg, etc)
log "Optimizing journald for Android and applying hardening..."
$CAT >> "$ROOTFS_PATH/etc/systemd/journald.conf" << 'EOT'
[Journal]
ReadKMsg=no
Audit=no
Storage=volatile
EOT

$MKDIR -p "$ROOTFS_PATH/etc/systemd/journald.conf.d"
$CAT > "$ROOTFS_PATH/etc/systemd/journald.conf.d/ds-logging.conf" << 'EOT'
[Journal]
SystemMaxUse=200M
RuntimeMaxUse=200M
MaxRetentionSec=7day
MaxLevelStore=info
EOT

# Enable essential services
log "Enabling essential systemd services..."
$MKDIR -p "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants"
for service in dbus.service systemd-udevd.service systemd-resolved.service systemd-networkd.service NetworkManager.service; do
    if $TEST -f "$ROOTFS_PATH/$GUEST_SYSTEMD_PATH/$service"; then
        $LN -sf "$GUEST_SYSTEMD_PATH/$service" "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/$service"
    fi
done

# Disable power button handling in systemd-logind
log "Disabling power/suspend button handling in systemd-logind..."
$MKDIR -p "$ROOTFS_PATH/etc/systemd/logind.conf.d"
$CAT > "$ROOTFS_PATH/etc/systemd/logind.conf.d/99-power-key.conf" << 'EOF'
[Login]
HandlePowerKey=ignore
HandleSuspendKey=ignore
HandleHibernateKey=ignore
HandlePowerKeyLongPress=ignore
HandlePowerKeyLongPressHibernate=ignore
EOF

# Apply udev overrides
log "Applying udev overrides..."
# 1. Trigger override (Prevents coldplugging Android hardware)
OVERRIDE_DIR="$ROOTFS_PATH/etc/systemd/system/systemd-udev-trigger.service.d"
$MKDIR -p "$OVERRIDE_DIR"
$CAT > "$OVERRIDE_DIR/override.conf" << 'EOF'
[Service]
ExecStart=
ExecStart=-/usr/bin/udevadm trigger --subsystem-match=usb --subsystem-match=block --subsystem-match=input --subsystem-match=tty
EOF

# 2. Read-only path overrides to prevent failures
for unit in systemd-udevd.service systemd-udev-trigger.service systemd-udev-settle.service systemd-udevd-kernel.socket systemd-udevd-control.socket; do
    $MKDIR -p "$ROOTFS_PATH/etc/systemd/system/${unit}.d"
    $PRINTF "[Unit]\nConditionPathIsReadWrite=\n" > "$ROOTFS_PATH/etc/systemd/system/${unit}.d/99-readonly-fix.conf"
done

# Configure logrotate
log "Configuring logrotate for Android..."
if $TEST -f "$ROOTFS_PATH/etc/logrotate.conf"; then
    $SED -i 's/^#maxsize.*/maxsize 50M/' "$ROOTFS_PATH/etc/logrotate.conf"
    if ! $GREP -q "maxsize 50M" "$ROOTFS_PATH/etc/logrotate.conf"; then
        $ECHO "maxsize 50M" >> "$ROOTFS_PATH/etc/logrotate.conf"
    fi
fi

# Mark fixes as completed
$ECHO "Post-extraction fixes applied on $(date)" > "$ROOTFS_PATH/etc/droidspaces"

log "Post-extraction fixes completed successfully"
