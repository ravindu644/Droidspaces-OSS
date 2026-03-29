#!/system/bin/sh
# Droidspaces post-fs-data script
# This script starts the Droidspaces daemon as early as possible during boot.

MODDIR=${0%/*}
DROIDSPACE_DIR=/data/local/Droidspaces
LOGS_DIR=${DROIDSPACE_DIR}/Logs
LOGS_FILE=${LOGS_DIR}/boot-module.log
DROIDSPACE_BINARY=${DROIDSPACE_DIR}/bin/droidspaces
BUSYBOX_BINARY=${DROIDSPACE_DIR}/bin/busybox
MAGISKPOLICY_BINARY=${DROIDSPACE_DIR}/bin/magiskpolicy
DROIDSPACES_TE_FILE=${MODDIR}/etc/droidspaces.te

# Create logs directory if it doesn't exist
mkdir -p "${LOGS_DIR}" 2>/dev/null

# Clear log files at boot start
> "${LOGS_FILE}" 2>/dev/null
> "${LOGS_DIR}/dmesg.log" 2>/dev/null

# Start dmesg logger as early as possible so we capture the full boot.
# We check common paths since not all devices have it in the same place.
# The PID is saved so service.sh can kill it cleanly after autoboot finishes.
DMESG_PID_FILE="${DROIDSPACE_DIR}/.dmesg_pid"
if [ -f /system/bin/dmesg ]; then
    DMESG_BIN=/system/bin/dmesg
elif [ -f /vendor/bin/dmesg ]; then
    DMESG_BIN=/vendor/bin/dmesg
elif [ -f /system/vendor/bin/dmesg ]; then
    DMESG_BIN=/system/vendor/bin/dmesg
else
    DMESG_BIN=dmesg
fi
"${DMESG_BIN}" -w >> "${LOGS_DIR}/dmesg.log" 2>/dev/null &
echo $! > "${DMESG_PID_FILE}"

# Function to log with timestamp
log() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "$(date +%s)")
    echo "[${timestamp}] [post-fs-data] $*" >> "${LOGS_FILE}"
}

log "Droidspaces post-fs-data script started"

# Daemon mode marker file
DAEMON_MODE_FILE=${DROIDSPACE_DIR}/.daemon_mode

# Check if the /vendor partition already has droidspaces binary intergrated
if [ -f /vendor/bin/droidspaces ] || [ -L /vendor/bin/droidspaces ]; then
    log "Droidspaces binary already integrated in /vendor partition, skipping..."
    echo "1" > "${DAEMON_MODE_FILE}"
    exit 0
fi

# Live SELinux patching
if [ -f "${MAGISKPOLICY_BINARY}" ] && [ -f "${DROIDSPACES_TE_FILE}" ]; then
    log "Patching SELinux policy..."
    OUTPUT=$("${MAGISKPOLICY_BINARY}" --live --apply "${DROIDSPACES_TE_FILE}" 2>&1)
    RET=$?
    if [ $RET -eq 0 ]; then
        log "SELinux policy patched successfully"
    else
        log "WARNING: magiskpolicy failed (exit $RET)"
        log "Output: ${OUTPUT}"
    fi
else
    log "Skipping live SELinux patch: magiskpolicy or .te file missing"
fi

# Start the Droidspaces daemon if enabled (value 1)
if [ -f "${DAEMON_MODE_FILE}" ] && [ "$(${BUSYBOX_BINARY} cat "${DAEMON_MODE_FILE}" 2>/dev/null)" = "1" ]; then
    log "Daemon mode enabled, starting Droidspaces daemon..."

    # Relabel the binary to droidspacesd_exec so the kernel's SELinux entrypoint
    # check passes when droidspaces re-execs itself into u:r:droidspacesd:s0.
    # Without this, the exec transition gets denied because system_data_file is
    # not a valid entrypoint for the droidspacesd domain.
    # This is a no-op if SELinux is disabled or the label is already correct.
    chcon u:object_r:droidspacesd_exec:s0 "${DROIDSPACE_BINARY}" 2>/dev/null || \
        log "WARNING: chcon failed - SELinux transition may fall back to setcon path"

    if "${DROIDSPACE_BINARY}" daemon >> "${LOGS_FILE}" 2>&1; then
        log "Daemon process launched successfully"
    else
        log "WARNING: Failed to launch daemon"
    fi
else
    log "Daemon mode disabled or not configured, skipping daemon start"
fi
