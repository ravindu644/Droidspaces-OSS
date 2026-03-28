#!/vendor/bin/sh

# Droidspaces Container Auto-Boot script
# Wired as a oneshot service triggered on sys.boot_completed=1
# Also triggered on droidspacesd service restart
# Handles run_at_boot containers for users on the native init path.
#
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

DROIDSPACE_DIR=/data/local/Droidspaces
LOGS_DIR=${DROIDSPACE_DIR}/Logs
LOGS_FILE=${LOGS_DIR}/boot-module.log
CONTAINERS_DIR=${DROIDSPACE_DIR}/Containers
DROIDSPACE_BINARY=/vendor/bin/droidspaces

mkdir -p "${LOGS_DIR}" 2>/dev/null
exec >> "${LOGS_FILE}" 2>&1

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || date +%s)] [autoboot] $*"
}

strip_colors() {
    sed "s/$(printf '\033')\[[0-9;]*[mK]//g"
}

wait_for_network() {
    local timeout=60
    local count=0
    log "Waiting for network..."
    while [ $count -lt $timeout ]; do
        if ip route get 8.8.8.8 2>/dev/null | grep -qv "ds-br0"; then
            log "Network is ready (${count}s)"
            return 0
        fi
        sleep 1
        count=$((count + 1))
    done
    log "WARNING: Network not ready after ${timeout}s, proceeding anyway"
    return 1
}

log "Droidspaces autoboot started"

# Sanity check
if [ ! -f "${DROIDSPACE_BINARY}" ] || [ ! -x "${DROIDSPACE_BINARY}" ]; then
    log "ERROR: Binary not found or not executable at ${DROIDSPACE_BINARY}, aborting"
    exit 1
fi

# Without this, the containers are starting too early
# Breaking container's networking :)
wait_for_network

# Scan and boot containers
log "Scanning for containers with run_at_boot=1..."
success=0
failed=0

for cfg in $(find "${CONTAINERS_DIR}" -name "container.config" 2>/dev/null); do
    [ -f "${cfg}" ] || continue

    run_at_boot=$(grep "^run_at_boot=" "${cfg}" 2>/dev/null | \
        head -1 | sed 's/^[^=]*=//' | tr -d '\r\n')

    [ "${run_at_boot}" = "1" ] || continue

    name=$(grep "^name=" "${cfg}" 2>/dev/null | \
        head -1 | sed 's/^[^=]*=//' | tr -d '\r\n')
    display="${name:-$(basename "$(dirname "${cfg}")")}"

    log "Starting container: ${display}"
    "${DROIDSPACE_BINARY}" --config "${cfg}" start 2>&1 | strip_colors

    if [ $? -eq 0 ]; then
        log "SUCCESS: ${display}"
        success=$((success + 1))
    else
        log "FAILED: ${display}"
        failed=$((failed + 1))
    fi
done

log "Autoboot complete: ${success} started | ${failed} failed"
