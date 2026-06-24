#!/system/bin/sh
set -o pipefail

# Droidspaces Magisk Module - Boot Service
MODDIR=${0%/*}
DROIDSPACE_DIR=/data/local/Droidspaces
LOGS_DIR=${DROIDSPACE_DIR}/Logs
LOGS_FILE=${LOGS_DIR}/boot-module.log
CONTAINERS_DIR=${DROIDSPACE_DIR}/Containers
DROIDSPACE_BINARY=${DROIDSPACE_DIR}/bin/droidspaces
BUSYBOX_BINARY=${DROIDSPACE_DIR}/bin/busybox
DAEMON_MODE_FILE=${DROIDSPACE_DIR}/.daemon_mode
DMESG_PID_FILE=${DROIDSPACE_DIR}/.dmesg_pid

mkdir -p "${LOGS_DIR}" 2>/dev/null
exec >> "${LOGS_FILE}" 2>&1

log() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "$(date +%s)")
    echo "[${timestamp}] [service] $*"
}

# Kill the dmesg -w process started in post-fs-data.sh.
# Called at the end of both the native init path and the Magisk path
# so we always stop capturing once autoboot is done.
stop_dmesg_logger() {
    if [ -f "${DMESG_PID_FILE}" ]; then
        DMESG_PID="$(cat "${DMESG_PID_FILE}" 2>/dev/null)"
        if [ -n "${DMESG_PID}" ] && kill -0 "${DMESG_PID}" 2>/dev/null; then
            kill "${DMESG_PID}" 2>/dev/null
            log "dmesg logger stopped (PID ${DMESG_PID})"
        fi
        rm -f "${DMESG_PID_FILE}"
    fi
}

update_prop() {
    sed -i "s/^description=.*/description=$*/g" "$MODDIR/module.prop"
}

get_daemon_pid() {
    pgrep -f "droidspaces daemon" 2>/dev/null | head -1
}

log "Droidspaces service.sh started"

# Native init path
# If /vendor/bin/droidspaces exists (real binary or symlink), init owns the
# daemon and the run-at-boot logic. This module steps aside entirely.
if [ -f /vendor/bin/droidspaces ] || [ -L /vendor/bin/droidspaces ]; then
    log "Native init integration detected."

    PID=$(get_daemon_pid)
    if [ -n "${PID}" ]; then
        log "Daemon is running under init (PID ${PID})"
        update_prop "🟢 Native init mode | Daemon PID: ${PID} | run-at-boot handled by init"
    else
        log "WARNING: /vendor/bin/droidspaces found but daemon is not running"
        update_prop "🔴 Found daemon integrated in /vendor/bin/droidspaces, but seems like it's not running!"
    fi

    # Wait for boot_completed so dmesg captures the autoboot service logs
    # before we shut the logger down
    while [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ]; do
        sleep 1
    done
    # Give droidspaces_autoboot a moment to finish running
    sleep 5
    stop_dmesg_logger
    exit 0
fi

# Magisk module path
log "Native init not detected, using Magisk module mode"

# Sanity checks
for bin in "${DROIDSPACE_BINARY}" "${BUSYBOX_BINARY}"; do
    if [ ! -f "${bin}" ]; then
        log "ERROR: Required binary not found: ${bin}"
        update_prop "🔴 Error: Missing binary: $(basename ${bin})"
        exit 1
    fi
done

# Apply correct SELinux context to rootfs images
log "Applying SELinux context to rootfs images..."
${BUSYBOX_BINARY} find "${CONTAINERS_DIR}" -name "*.img" \
    -exec chcon u:object_r:vold_data_file:s0 {} + 2>/dev/null

# Wait for full boot
log "Waiting for boot to complete..."
while [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ]; do
    sleep 1
done

wait_for_network() {
    log "Boot completed, waiting for network..."

    # Fallback for devices without /system/bin/ip
    if [ ! -x /system/bin/ip ]; then
        log "WARNING: /system/bin/ip not found, sleeping for 25 seconds as a fallback"
        sleep 25
        return 0
    fi

    local timeout=25
    local count=0
    while [ $count -lt $timeout ]; do
        if /system/bin/ip route get 8.8.8.8 2>/dev/null | grep -qv "ds-br0"; then
            log "Network is ready (${count}s). Waiting 10s for stability..."
            sleep 10
            return 0
        fi
        sleep 1
        count=$((count + 1))
    done
    log "WARNING: Network not ready after ${timeout}s, proceeding anyway"
    return 1
} && wait_for_network

# Check daemon status
DAEMON_STATUS="⚪ Off"
if [ -f "${DAEMON_MODE_FILE}" ] && \
   [ "$(${BUSYBOX_BINARY} cat "${DAEMON_MODE_FILE}" 2>/dev/null)" = "1" ]; then

    log "Daemon mode enabled, checking status..."

    count=0
    while [ ${count} -lt 5 ]; do
        PID=$(get_daemon_pid)
        if [ -n "${PID}" ]; then
            log "Daemon running (PID ${PID})"
            DAEMON_STATUS="🟢 Running (PID ${PID})"
            break
        fi
        sleep 1
        count=$((count + 1))
    done

    if [ "${DAEMON_STATUS}" = "⚪ Off" ]; then
        log "WARNING: Daemon not found after 5 seconds"
        DAEMON_STATUS="🔴 Not Running"
    fi
else
    log "Daemon mode disabled, skipping"
fi

# Auto-boot containers (priority-ordered, gateway-aware)
#
# Containers boot from the smallest run_at_boot_priority to the largest; missing
# or invalid priorities sort last (best-effort), then alphabetically. A
# net_mode=gateway client is skipped if its gateway_container did not come up,
# so we never boot a router-less client. Independent containers always boot, and
# a single failure never blocks the rest.
log "Scanning for containers with run_at_boot=1..."
success=0
failed=0
skipped=0

TAB=$(${BUSYBOX_BINARY} printf '\t')
BOOT_TABLE="${DROIDSPACE_DIR}/.autoboot_table"
SORTED_FILE="${DROIDSPACE_DIR}/.autoboot_sorted"
STARTED_OK="${DROIDSPACE_DIR}/.autoboot_started"
: > "${BOOT_TABLE}"
: > "${STARTED_OK}"

# Read a single key=value from a config file
get_cfg_val() {
    ${BUSYBOX_BINARY} grep "^$1=" "$2" 2>/dev/null | \
        ${BUSYBOX_BINARY} head -1 | ${BUSYBOX_BINARY} sed 's/^[^=]*=//' | \
        ${BUSYBOX_BINARY} tr -d '\r\n'
}

# Find the container.config whose name= matches $1 (echoes path, empty if none)
cfg_for_name() {
    for c in $(${BUSYBOX_BINARY} find "${CONTAINERS_DIR}" -name "container.config" 2>/dev/null); do
        n=$(get_cfg_val name "$c")
        [ -n "$n" ] || n=$(${BUSYBOX_BINARY} basename "$(${BUSYBOX_BINARY} dirname "$c")")
        if [ "$n" = "$1" ]; then
            echo "$c"
            return 0
        fi
    done
    return 1
}

# A gateway is available if it started during this run, or is already running
gateway_available() {
    if ${BUSYBOX_BINARY} grep -qxF "$1" "${STARTED_OK}" 2>/dev/null; then
        return 0
    fi
    gcfg=$(cfg_for_name "$1")
    [ -n "${gcfg}" ] || return 1
    gpid=$("${DROIDSPACE_BINARY}" --config "${gcfg}" pid 2>/dev/null)
    [ "${gpid}" != "NONE" ] && [ -n "${gpid}" ]
}

# Start one container; returns 0 if it ends up with a live PID
boot_container() {
    "${DROIDSPACE_BINARY}" --config "$1" start 2>&1 | \
        ${BUSYBOX_BINARY} sed "s/$(${BUSYBOX_BINARY} printf '\033')\[[0-9;]*[mK]//g"
    bpid=$("${DROIDSPACE_BINARY}" --config "$1" pid 2>/dev/null)
    [ "${bpid}" != "NONE" ] && [ -n "${bpid}" ]
}

# Build the boot table: PRIORITY \t NAME \t NET_MODE \t GATEWAY \t CFG
for cfg in $(${BUSYBOX_BINARY} find "${CONTAINERS_DIR}" -name "container.config" 2>/dev/null); do
    [ -f "${cfg}" ] || continue
    [ "$(get_cfg_val run_at_boot "${cfg}")" = "1" ] || continue

    name=$(get_cfg_val name "${cfg}")
    [ -n "${name}" ] || name=$(${BUSYBOX_BINARY} basename "$(${BUSYBOX_BINARY} dirname "${cfg}")")

    prio=$(get_cfg_val run_at_boot_priority "${cfg}")
    case "${prio}" in
        ''|*[!0-9]*) prio=99999999 ;;
    esac

    # '-' placeholder for empty fields: IFS=<tab> collapses adjacent tabs
    # (tab is IFS whitespace), so empty middle fields would shift the columns.
    netmode=$(get_cfg_val net_mode "${cfg}")
    [ -n "${netmode}" ] || netmode="-"
    gw=$(get_cfg_val gateway_container "${cfg}")
    [ -n "${gw}" ] || gw="-"

    ${BUSYBOX_BINARY} printf '%s\t%s\t%s\t%s\t%s\n' "${prio}" "${name}" "${netmode}" "${gw}" "${cfg}" >> "${BOOT_TABLE}"
done

# Smallest priority first, then name. Gaps/holes and large sentinels sort cleanly.
${BUSYBOX_BINARY} sort -t"${TAB}" -k1,1n -k2,2 "${BOOT_TABLE}" > "${SORTED_FILE}"

# Reading from a file (not a pipe) keeps the loop in this shell so the counters
# survive each iteration.
while IFS="${TAB}" read -r prio name netmode gw cfg; do
    [ -n "${cfg}" ] || continue
    [ "${gw}" = "-" ] && gw=""

    if [ "${netmode}" = "gateway" ] && [ -n "${gw}" ]; then
        if ! gateway_available "${gw}"; then
            log "SKIP: ${name} (gateway '${gw}' not running)"
            skipped=$((skipped + 1))
            continue
        fi
    fi

    log "Starting container: ${name}"
    if boot_container "${cfg}"; then
        PID=$("${DROIDSPACE_BINARY}" --config "${cfg}" pid 2>/dev/null)
        log "SUCCESS: ${name} (PID: ${PID})"
        ${BUSYBOX_BINARY} printf '%s\n' "${name}" >> "${STARTED_OK}"
        success=$((success + 1))
    else
        log "FAILED: ${name}"
        failed=$((failed + 1))
    fi
done < "${SORTED_FILE}"

${BUSYBOX_BINARY} rm -f "${BOOT_TABLE}" "${SORTED_FILE}" "${STARTED_OK}" 2>/dev/null

stop_dmesg_logger
log "Boot Summary: Daemon: ${DAEMON_STATUS} | ${success} started | ${failed} failed | ${skipped} skipped"
update_prop "Daemon: ${DAEMON_STATUS} | Containers: ${success} started, ${failed} failed, ${skipped} skipped"
