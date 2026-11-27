#!/system/bin/sh
# Post-Extraction Fixes for Linux on Android
# Copyright (c) 2026 ravindu644
# Applies generic fixes after rootfs tarball extraction
# This script runs after extraction but before unmounting
#
# Every fix here mirrors one applied at build time by the Dockerfiles in
# Droidspaces-rootfs-builder, so a user-supplied tarball ends up with the same
# Android compatibility as a first-party one. Each fix is guarded by what is
# actually present in the rootfs, never by assumption.

set -e

# Parameters
ROOTFS_PATH="$1"
BUSYBOX_PATH="${BUSYBOX_PATH:-/data/local/Droidspaces/bin/busybox}"

# Check if BusyBox exists
if [ ! -x "$BUSYBOX_PATH" ]; then
    echo "[POST-FIX-ERROR] BusyBox not found or not executable at $BUSYBOX_PATH" >&2
    exit 1
fi

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
TOUCH="$BB touch"
CP="$BB cp"

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

# Helper to execute a command inside the chroot environment
run_in_chroot() {
    local command="$*"
    local common_exports="export PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/libexec:/opt/bin'; export TMPDIR='/tmp';"

    # We use busybox chroot to run commands inside the rootfs
    # Note: This assumes /bin/sh exists in the rootfs
    $CHROOT "$ROOTFS_PATH" /bin/sh -c "$common_exports $command"
}

# OpenRC runlevel wiring without rc-update: rc-update needs /run/openrc, which
# does not exist in an unbooted rootfs, so link the scripts by hand like the
# Alpine Dockerfiles do. Both are no-ops when the service script is missing.
rc_enable() {
    $TEST -f "$ROOTFS_PATH/etc/init.d/$1" || return 0
    $MKDIR -p "$ROOTFS_PATH/etc/runlevels/$2"
    $LN -sf "/etc/init.d/$1" "$ROOTFS_PATH/etc/runlevels/$2/$1"
}
rc_disable() {
    $RM -f "$ROOTFS_PATH/etc/runlevels/$2/$1"
}

log "Starting post-extraction fixes for: $ROOTFS_PATH"

# Detect NixOS
if $TEST -d "$ROOTFS_PATH/nix"; then
    log "NixOS detected, skipping all post-extraction fixes (Nix manages its own state)"
    # Mark as applied anyway to prevent re-running
    $TOUCH "$ROOTFS_PATH/etc/droidspaces" 2>/dev/null || true
    exit 0
fi

# Pre-commit machine-id to prevent first-boot deadlock in Fedora 44
log "Generating machine-id..."
$CAT /proc/sys/kernel/random/uuid | $BB tr -d '-' | $BB tr -d '\n' > "$ROOTFS_PATH/etc/machine-id"
$ECHO "" >> "$ROOTFS_PATH/etc/machine-id"
$CHMOD 444 "$ROOTFS_PATH/etc/machine-id"

# Check if fixes were already applied
if $TEST -f "$ROOTFS_PATH/etc/droidspaces"; then
    log "Post-extraction fixes already applied, skipping..."
    exit 0
fi

# --- 1. General Fixes (Init-independent) ---

# Android network group setup (required for socket access on Android kernels)
log "Setting up Android network groups..."
$GREP -q '^aid_inet:' "$ROOTFS_PATH/etc/group"    || $ECHO 'aid_inet:x:3003:'    >> "$ROOTFS_PATH/etc/group"
$GREP -q '^aid_net_raw:' "$ROOTFS_PATH/etc/group" || $ECHO 'aid_net_raw:x:3004:' >> "$ROOTFS_PATH/etc/group"
$GREP -q '^aid_net_admin:' "$ROOTFS_PATH/etc/group" || $ECHO 'aid_net_admin:x:3005:' >> "$ROOTFS_PATH/etc/group"

# Root gets required permissions for networking, input, and display
log "Granting root permissions for Android hardware access..."
if $TEST -x "$ROOTFS_PATH/usr/sbin/usermod" || $TEST -x "$ROOTFS_PATH/usr/bin/usermod" || $TEST -x "$ROOTFS_PATH/sbin/usermod"; then
    run_in_chroot "usermod -a -G aid_inet,aid_net_raw,input,video,tty root 2>/dev/null || true"
else
    # BusyBox rootfs (OpenWrt, bare Alpine) have no usermod: put root in the
    # member list directly, the way the OpenWrt Dockerfile writes it.
    for grp in aid_inet aid_net_raw; do
        $GREP -qE "^$grp:[^:]*:[^:]*:.*\broot\b" "$ROOTFS_PATH/etc/group" || \
            $SED -i "s/^\($grp:[^:]*:[^:]*:\)\(.*\)$/\1\2,root/; s/:,root$/:root/" "$ROOTFS_PATH/etc/group"
    done
fi

# The package manager's sandbox user needs aid_inet as primary group so it can
# open sockets on Android: _apt on Debian family, _tdnf on Azure Linux.
log "Fixing package manager user group for internet access..."
run_in_chroot "grep -q '^_apt:' /etc/passwd && usermod -g aid_inet _apt 2>/dev/null || true"
run_in_chroot "grep -q '^_tdnf:' /etc/passwd && usermod -g aid_inet _tdnf 2>/dev/null || true"

# Future users created with adduser automatically get network access
if $TEST -f "$ROOTFS_PATH/etc/adduser.conf"; then
    log "Configuring adduser for automatic Android group assignment..."
    $SED -i '/^EXTRA_GROUPS=/d; /^ADD_EXTRA_GROUPS=/d' "$ROOTFS_PATH/etc/adduser.conf"
    $ECHO 'ADD_EXTRA_GROUPS=1' >> "$ROOTFS_PATH/etc/adduser.conf"
    $ECHO 'EXTRA_GROUPS="aid_inet aid_net_raw input video tty"' >> "$ROOTFS_PATH/etc/adduser.conf"
fi

# Android kernels ship x_tables only, no nf_tables. Every xtables frontend must
# be the legacy one or iptables silently fails inside the container. Debian
# family registers alternatives, everyone else gets a plain symlink.
log "Switching xtables frontends to the legacy backend..."
for tool in iptables ip6tables arptables ebtables; do
    if $TEST -f "$ROOTFS_PATH/var/lib/dpkg/alternatives/$tool" && $TEST -x "$ROOTFS_PATH/usr/sbin/$tool-legacy"; then
        run_in_chroot "update-alternatives --set $tool /usr/sbin/$tool-legacy 2>/dev/null" || true
        continue
    fi
    for dir in usr/sbin usr/bin sbin; do
        if $TEST -x "$ROOTFS_PATH/$dir/$tool-legacy"; then
            $LN -sf "/$dir/$tool-legacy" "$ROOTFS_PATH/$dir/$tool"
            break
        fi
    done
done

# NetworkManager DHCP profile for the container's veth (eth*). Any init system.
# NetworkManager refuses connection files that are not mode 0600.
if $TEST -d "$ROOTFS_PATH/etc/NetworkManager"; then
    log "Writing NetworkManager DHCP profile for eth* interfaces..."
    $MKDIR -p "$ROOTFS_PATH/etc/NetworkManager/system-connections"
    $CHMOD 700 "$ROOTFS_PATH/etc/NetworkManager/system-connections"
    $CAT > "$ROOTFS_PATH/etc/NetworkManager/system-connections/droidspaces-ethernet.nmconnection" << 'EOF'
[connection]
id=droidspaces-ethernet
type=ethernet
autoconnect=true

[match]
interface-name=eth*

[ipv4]
method=auto
route-metric=100

[ipv6]
method=auto
addr-gen-mode=stable-privacy
EOF
    $CHMOD 600 "$ROOTFS_PATH/etc/NetworkManager/system-connections/droidspaces-ethernet.nmconnection"
fi

# elogind (Artix, Devuan, Gentoo) reacts to the phone's power key exactly like
# systemd-logind: with hardware access the key is an input device, udev tags it
# power-switch and the default HandlePowerKey=poweroff shuts the container down.
if $TEST -d "$ROOTFS_PATH/etc/elogind"; then
    log "Disabling power/suspend button handling in elogind..."
    $MKDIR -p "$ROOTFS_PATH/etc/elogind/logind.conf.d"
    $CAT > "$ROOTFS_PATH/etc/elogind/logind.conf.d/99-power-key.conf" << 'EOF'
[Login]
HandlePowerKey=ignore
HandleSuspendKey=ignore
HandleHibernateKey=ignore
HandlePowerKeyLongPress=ignore
HandlePowerKeyLongPressHibernate=ignore
EOF
fi

# --- 2. Systemd-Specific Fixes ---

# Check if systemd is available
if $TEST -f "$ROOTFS_PATH/usr/bin/systemctl" || $TEST -f "$ROOTFS_PATH/bin/systemctl"; then
    GUEST_SYSTEMD_PATH="/lib/systemd/system"
    $TEST -d "$ROOTFS_PATH/usr/lib/systemd/system" && GUEST_SYSTEMD_PATH="/usr/lib/systemd/system"

    log "Systemd detected (at $GUEST_SYSTEMD_PATH), applying fixes..."

    # 01. Mask problematic services for Android kernels
    log "Masking problematic systemd services..."
    $MKDIR -p "$ROOTFS_PATH/etc/systemd/system"
    # Mask systemd-networkd-wait-online.service
    $LN -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-networkd-wait-online.service"
    # Mask systemd-journald-audit.socket to prevent deadlocks on Android kernels
    $LN -sf /dev/null "$ROOTFS_PATH/etc/systemd/system/systemd-journald-audit.socket"

    # Nuke problematic iptables service
    for f in \
        "$ROOTFS_PATH/etc/systemd/system/iptables.service" \
        "$ROOTFS_PATH/etc/systemd/scripts/iptables" \
        "$ROOTFS_PATH/etc/systemd/scripts/iptables.stop" \
        "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/iptables.service"
    do
        $TEST -f "$f" && $RM -f "$f"
    done

    # 02. Journald configuration (skip Audit, KMsg, etc)
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

    # 03. Enable essential services
    log "Enabling essential systemd services..."
    $MKDIR -p "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants"
    for service in dbus.service systemd-udevd.service systemd-resolved.service systemd-networkd.service NetworkManager.service; do
        if $TEST -f "$ROOTFS_PATH/$GUEST_SYSTEMD_PATH/$service"; then
            $LN -sf "$GUEST_SYSTEMD_PATH/$service" "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/$service"
        fi
    done

    # 04. Disable power button handling in systemd-logind
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

    # 05. Apply udev overrides
    log "Applying udev overrides..."
    # 05a. Trigger override (Prevents coldplugging Android hardware)
    OVERRIDE_DIR="$ROOTFS_PATH/etc/systemd/system/systemd-udev-trigger.service.d"
    $MKDIR -p "$OVERRIDE_DIR"
    $CAT > "$OVERRIDE_DIR/override.conf" << 'EOF'
[Service]
ExecStart=
ExecStart=-/usr/bin/udevadm trigger --subsystem-match=usb --subsystem-match=block --subsystem-match=input --subsystem-match=tty --subsystem-match=net
EOF

    # 05b. Read-only path overrides to prevent failures
    for unit in systemd-udevd.service systemd-udev-trigger.service systemd-udev-settle.service systemd-udevd-kernel.socket systemd-udevd-control.socket; do
        $MKDIR -p "$ROOTFS_PATH/etc/systemd/system/${unit}.d"
        $PRINTF "[Unit]\nConditionPathIsReadWrite=\n" > "$ROOTFS_PATH/etc/systemd/system/${unit}.d/99-readonly-fix.conf"
    done

    # 06. Keep network services out of host and none modes
    # Every mode with its own network namespace needs an in-container DHCP
    # client (NAT: lease from Droidspaces; gateway: from the gateway container;
    # macvlan: from the LAN). Host mode skips them so they never touch Android's
    # interfaces, none mode because there is no eth0. Listing the two modes to
    # skip, rather than the ones to run in, keeps a new mode working on rootfs
    # installed before it existed.
    log "Applying network mode guards to network services..."
    for unit in NetworkManager.service dhcpcd.service systemd-resolved.service systemd-networkd.service; do
        if $TEST -f "$ROOTFS_PATH/$GUEST_SYSTEMD_PATH/$unit" || $TEST -e "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/$unit"; then
            $MKDIR -p "$ROOTFS_PATH/etc/systemd/system/${unit}.d"
            $CAT > "$ROOTFS_PATH/etc/systemd/system/${unit}.d/99-netmode-limit.conf" << 'EOF'
[Service]
ExecCondition=
ExecCondition=/bin/sh -c "grep -q '^net_mode=' /run/droidspaces/container.config && ! grep -qE '^net_mode=(host|none)$' /run/droidspaces/container.config"
EOF
        fi
    done

    # 07. Configure systemd-networkd for eth* interfaces
    log "Configuring systemd network for eth* interfaces..."
    $MKDIR -p "$ROOTFS_PATH/etc/systemd/network"
    $CAT > "$ROOTFS_PATH/etc/systemd/network/10-eth-dhcp.network" << 'EOF'
[Match]
Name=eth*

[Network]
DHCP=yes
IPv6AcceptRA=yes

[DHCPv4]
UseDNS=yes
UseDomains=yes
RouteMetric=100
EOF

    # 07b. Macvlan mode on a NetworkManager-only rootfs. NetworkManager will not
    # manage a macvlan whose parent is in another namespace and there is no
    # systemd-networkd to fall back on, so dhcpcd serves eth0 in that mode only.
    # Manager mode follows eth0 across a replug; --nodev because udev never
    # announces a device born in the container's namespace. Skipped when the
    # distro's own dhcpcd.service runs, which then covers eth0 itself.
    if $TEST -f "$ROOTFS_PATH/$GUEST_SYSTEMD_PATH/NetworkManager.service" &&
        ! $TEST -f "$ROOTFS_PATH/$GUEST_SYSTEMD_PATH/systemd-networkd.service" &&
        ! $TEST -e "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/dhcpcd.service" &&
        { $TEST -x "$ROOTFS_PATH/usr/sbin/dhcpcd" || $TEST -x "$ROOTFS_PATH/sbin/dhcpcd"; }; then
        log "Adding the macvlan dhcpcd fallback for NetworkManager..."
        $CAT > "$ROOTFS_PATH/etc/systemd/system/droidspaces-macvlan-dhcp.service" << 'EOF'
[Unit]
Description=DHCP for the Droidspaces macvlan eth0
After=network-pre.target

[Service]
ExecCondition=/bin/sh -c "grep -qs '^net_mode=macvlan$' /run/droidspaces/container.config"
ExecStart=/bin/sh -c "exec dhcpcd -M -B --nodev -z eth0"

[Install]
WantedBy=multi-user.target
EOF
        $LN -sf /etc/systemd/system/droidspaces-macvlan-dhcp.service "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/droidspaces-macvlan-dhcp.service"
    fi

    # 08. Mount binfmt_misc at boot so qemu-user-static handlers register.
    # Only worth installing when a qemu-*-static binary is present.
    if $BB ls "$ROOTFS_PATH"/usr/bin/qemu-*-static >/dev/null 2>&1; then
        log "Installing qemu binfmt registration unit..."
        $MKDIR -p "$ROOTFS_PATH/usr/local/bin"
        $CAT > "$ROOTFS_PATH/usr/local/bin/qemu-binfmt-register.sh" << 'EOF'
#!/bin/sh
set -eu

BINFMT_MISC="/proc/sys/fs/binfmt_misc"

log() { echo "qemu-binfmt: $*"; }

# kernel support check
if ! grep -q binfmt_misc /proc/filesystems 2>/dev/null; then
    log "binfmt_misc not supported by kernel, skipping"
    exit 0
fi

# mount if needed
if ! grep -q "$BINFMT_MISC" /proc/mounts; then
    if ! mount -t binfmt_misc binfmt_misc "$BINFMT_MISC"; then
        log "failed to mount binfmt_misc, skipping"
        exit 0
    fi
fi

# success
exit 0
EOF
        $CHMOD 755 "$ROOTFS_PATH/usr/local/bin/qemu-binfmt-register.sh"
        $CAT > "$ROOTFS_PATH/etc/systemd/system/qemu-binfmt-register.service" << 'EOF'
[Unit]
Description=Register QEMU binfmt_misc handlers
DefaultDependencies=no
After=local-fs.target
Before=sysinit.target proc-sys-fs-binfmt_misc.automount

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/bin/qemu-binfmt-register.sh

[Install]
WantedBy=multi-user.target
EOF
        $CHMOD 644 "$ROOTFS_PATH/etc/systemd/system/qemu-binfmt-register.service"
        $LN -sf /etc/systemd/system/qemu-binfmt-register.service "$ROOTFS_PATH/etc/systemd/system/multi-user.target.wants/qemu-binfmt-register.service"
    fi

else
    log "Systemd not found, skipping systemd-specific fixes"
fi

# --- 3. OpenRC Fixes (Alpine, Artix) ---

if $TEST -f "$ROOTFS_PATH/etc/rc.conf" && $TEST -x "$ROOTFS_PATH/sbin/openrc-run"; then
    log "OpenRC detected, applying fixes..."

    # Tell OpenRC it runs in an LXC-style container.
    $SED -i 's/^#\?rc_sys=.*/rc_sys="lxc"/' "$ROOTFS_PATH/etc/rc.conf"

    # machine-id must not wait for a "dev" service Droidspaces never provides.
    if $TEST -f "$ROOTFS_PATH/etc/init.d/machine-id"; then
        $SED -i 's/need root dev/need root/' "$ROOTFS_PATH/etc/init.d/machine-id"
    fi

    # eudev under rc_sys="lxc": the stock script excludes itself from every
    # container type and needs sysfs, which Droidspaces mounts before init.
    # Replace the unrestricted coldplug with the same subsystem allow-list the
    # systemd override uses.
    if $TEST -f "$ROOTFS_PATH/etc/init.d/udev" && $GREP -q 'keyword -containers' "$ROOTFS_PATH/etc/init.d/udev"; then
        log "Enabling eudev with a restricted coldplug..."
        $SED -i -e '/keyword -containers/d' -e 's/need sysfs dev-mount/need dev-mount/' "$ROOTFS_PATH/etc/init.d/udev"
        $CAT > "$ROOTFS_PATH/etc/init.d/droidspaces-udev-trigger" << 'EOT'
#!/sbin/openrc-run

description="Restricted eudev coldplug for DroidSpaces"

depend() {
    need udev
    before localmount
}

start() {
    ebegin "Triggering container-safe eudev subsystems"
    udevadm trigger --type=subsystems --action=add \
        --subsystem-match=usb \
        --subsystem-match=block \
        --subsystem-match=input \
        --subsystem-match=tty \
        --subsystem-match=net
    udevadm trigger --type=devices --action=add \
        --subsystem-match=usb \
        --subsystem-match=block \
        --subsystem-match=input \
        --subsystem-match=tty \
        --subsystem-match=net
    eend $?
}
EOT
        $CHMOD 755 "$ROOTFS_PATH/etc/init.d/droidspaces-udev-trigger"
        rc_enable udev sysinit
        rc_disable udev-trigger sysinit
        rc_enable droidspaces-udev-trigger sysinit
    fi

    # NetworkManager only in modes with their own network, the OpenRC twin of the
    # systemd ExecCondition. dhcpcd leaves the default runlevel, NetworkManager
    # owns DHCP, except in macvlan mode: NetworkManager will not manage a macvlan
    # whose parent is in another namespace, so dhcpcd serves eth0 there. Manager
    # mode, so it follows eth0 when the host recreates it after a replug, and
    # --nodev, because udev never announces a device born in our namespace and
    # dhcpcd would wait on it forever.
    if $TEST -f "$ROOTFS_PATH/etc/init.d/NetworkManager"; then
        log "Gating NetworkManager on network mode..."
        $CAT > "$ROOTFS_PATH/etc/init.d/droidspaces-network" << 'EOT'
#!/sbin/openrc-run

description="Conditional DroidSpaces NetworkManager startup"

depend() {
    need dbus
    after udev droidspaces-udev-trigger
}

is_managed_mode() {
    grep -qs '^net_mode=' /run/droidspaces/container.config &&
        ! grep -qsE '^net_mode=(host|none)$' /run/droidspaces/container.config
}

is_macvlan_mode() {
    grep -qs '^net_mode=macvlan$' /run/droidspaces/container.config
}

start() {
    if ! is_managed_mode; then
        einfo "Host or none networking; leaving networking untouched"
        return 0
    fi

    ebegin "Starting NetworkManager for the container network"
    rc-service NetworkManager start
    # NetworkManager's script exits non-zero while "inactive" (started, not yet
    # online). The daemon is running either way, so don't fail this service on it.
    eend 0

    if is_macvlan_mode && command -v dhcpcd >/dev/null; then
        ebegin "Starting dhcpcd for the macvlan eth0"
        # Detached from our stdio, or a caller reading our output waits on it
        dhcpcd -M -b --nodev -z eth0 </dev/null >/dev/null 2>&1
        eend $?
    fi
}

stop() {
    is_managed_mode || return 0
    is_macvlan_mode && command -v dhcpcd >/dev/null && dhcpcd -x
    rc-service NetworkManager stop
}
EOT
        $CHMOD 755 "$ROOTFS_PATH/etc/init.d/droidspaces-network"
        rc_enable dbus default
        rc_enable droidspaces-network default
        rc_disable dhcpcd default
    fi

    # Artix boots with openrc-init and agetty.<port> services. Drop the VT
    # gettys and add one on /dev/console for the Droidspaces foreground console.
    if $TEST -f "$ROOTFS_PATH/etc/init.d/agetty" && $TEST -f "$ROOTFS_PATH/etc/conf.d/agetty.tty1"; then
        log "Replacing VT gettys with a console getty..."
        for n in 1 2 3 4 5 6; do rc_disable agetty.tty$n default; done
        $LN -sf agetty "$ROOTFS_PATH/etc/init.d/agetty.console"
        $CP "$ROOTFS_PATH/etc/conf.d/agetty.tty1" "$ROOTFS_PATH/etc/conf.d/agetty.console"
        rc_enable agetty.console default
    fi

    # /proc/sys is read-only in Droidspaces containers and the distro's sysctl.d
    # defaults mean nothing on an Android kernel. Artix keeps erroring on it.
    if $TEST -f "$ROOTFS_PATH/etc/os-release" && $GREP -q '^ID=artix$' "$ROOTFS_PATH/etc/os-release"; then
        rc_disable sysctl boot
    fi
fi

# BusyBox init (Alpine): no VTs in a container, so drop tty1-6 from inittab and
# put the getty on /dev/console instead. Only BusyBox inittab has the
# "id::action:" syntax; sysvinit files are left alone.
if $TEST -f "$ROOTFS_PATH/etc/inittab" && $GREP -q '::sysinit:' "$ROOTFS_PATH/etc/inittab"; then
    log "BusyBox inittab detected, moving getty to /dev/console..."
    $SED -i '/^tty[1-6]::/d' "$ROOTFS_PATH/etc/inittab"
    $GREP -q 'console::respawn' "$ROOTFS_PATH/etc/inittab" || \
        $ECHO 'console::respawn:/sbin/getty 38400 console' >> "$ROOTFS_PATH/etc/inittab"
    if $TEST -f "$ROOTFS_PATH/etc/securetty"; then
        $GREP -q '^console$' "$ROOTFS_PATH/etc/securetty" || $ECHO 'console' >> "$ROOTFS_PATH/etc/securetty"
    fi
fi

# --- 4. dhcpcd Fixes (any init) ---

# Replace dhcpcd init script to stay out of host and none network modes
# This is the OpenRC equivalent of systemd's ExecCondition - if the container
# is running in host network mode, dhcpcd is cleanly skipped at boot to prevent
# cellular network breakage and kernel panics on Android interfaces. Every
# other mode gets its lease through eth0.
if $TEST -f "$ROOTFS_PATH/etc/init.d/dhcpcd"; then
    log "Alpine/OpenRC dhcpcd service detected, applying network mode guard..."
    $CAT > "$ROOTFS_PATH/etc/init.d/dhcpcd" << 'INITEOF'
#!/sbin/openrc-run

description="DHCP Client Daemon"

command="/sbin/dhcpcd"
# --nodev: udev never announces a device born in the container's namespace,
# and dhcpcd would wait on it forever where udev runs
command_args="-q -B --nodev ${command_args:-}"
command_background="true"
pidfile="/run/dhcpcd/pid"

depend() {
	provide net
	need localmount
	use logger network
	after bootmisc modules
	before dns
}

start_pre() {
	# Not in host mode (cellular breakage) or none mode (no eth0)
	if ! grep -qs '^net_mode=' /run/droidspaces/container.config ||
		grep -qsE '^net_mode=(host|none)$' /run/droidspaces/container.config; then
		einfo "Skipping dhcpcd: host or none network mode"
		return 1
	fi
	checkpath -d /run/dhcpcd
}
INITEOF
    $CHMOD +x "$ROOTFS_PATH/etc/init.d/dhcpcd"
fi

# Additionally whitelist only container veth interfaces (eth*) in dhcpcd.conf
# as defense-in-depth against Android-internal interfaces (rmnet*, dit*, epdg*, etc.)
if $TEST -f "$ROOTFS_PATH/etc/dhcpcd.conf"; then
    log "dhcpcd.conf detected, whitelisting container eth* interfaces..."
    if ! $GREP -q "allowinterfaces eth\*" "$ROOTFS_PATH/etc/dhcpcd.conf"; then
        $ECHO "allowinterfaces eth*" >> "$ROOTFS_PATH/etc/dhcpcd.conf"
    fi
fi

# --- 5. Devuan-specific Fixes ---

if $TEST -f "$ROOTFS_PATH/etc/os-release" &&
   $GREP -q '^ID=devuan$' "$ROOTFS_PATH/etc/os-release"; then
    log "Devuan detected, patching umount init scripts..."

    for f in "$ROOTFS_PATH"/etc/init.d/umount*; do
        $TEST -f "$f" || continue

        # Avoid inserting the workaround multiple times
        if ! $GREP -q '^exit 0$' "$f"; then
            $SED -i '/^### END INIT INFO$/a exit 0' "$f"
        fi

        $CHMOD +x "$f"
    done
fi


# --- 6. OpenWrt Fixes ---

# debugfs has one superblock for the whole machine, so mounting it inside the
# container without a mode option resets the host's /sys/kernel/debug to 0700.
# Android's webview_zygote then aborts on its next fork and is never started
# again: Chrome and every WebView app crash until the phone reboots. OpenWrt
# mounts it from /etc/init.d/boot on every start and needs it for nothing here.
if $TEST -f "$ROOTFS_PATH/etc/init.d/boot" && $GREP -q -- '-t debugfs debugfs' "$ROOTFS_PATH/etc/init.d/boot"; then
    log "OpenWrt detected, removing the debugfs mount from /etc/init.d/boot..."
    $SED -i '/-t debugfs debugfs/d' "$ROOTFS_PATH/etc/init.d/boot"
fi

# --- 7. Miscellaneous Fixes ---

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
