#!/system/bin/sh

# Droidspaces Boot Script
# Automatically starts containers with run_at_boot=1 on device boot

DROIDSPACE_DIR=/data/local/Droidspaces
LOGS_DIR=${DROIDSPACE_DIR}/Logs
LOGS_FILE=${LOGS_DIR}/boot-module.log
CONTAINERS_DIR=${DROIDSPACE_DIR}/Containers
DROIDSPACE_BINARY=${DROIDSPACE_DIR}/bin/droidspaces
BUSYBOX_BINARY=${DROIDSPACE_DIR}/bin/busybox

# Create logs directory if it doesn't exist
mkdir -p "${LOGS_DIR}" 2>/dev/null

# Clear log file at boot start
> "${LOGS_FILE}" 2>/dev/null

# Redirect all output to log file
exec >> "${LOGS_FILE}" 2>&1

# Function to log with timestamp
log() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "$(date +%s)")
    echo "[${timestamp}] $*"
}

# Function to parse config file and extract a value
get_config_value() {
    local config_file="$1"
    local key="$2"
    ${BUSYBOX_BINARY} grep "^${key}=" "${config_file}" 2>/dev/null | ${BUSYBOX_BINARY} head -1 | ${BUSYBOX_BINARY} sed 's/^[^=]*=//' | ${BUSYBOX_BINARY} tr -d '\r\n'
}

log "Droidspaces boot module started"

# Check if droidspaces binary exists
if [ ! -f "${DROIDSPACE_BINARY}" ]; then
    log "ERROR: Droidspaces binary not found at ${DROIDSPACE_BINARY}"
    exit 1
fi

# Check if busybox exists
if [ ! -f "${BUSYBOX_BINARY}" ]; then
    log "ERROR: Busybox binary not found at ${BUSYBOX_BINARY}"
    exit 1
fi

log "All prerequisites checked successfully"

# Apply correct SELinux context to .img files to prevent mount I/O errors
log "Applying SELinux context to rootfs images..."
${BUSYBOX_BINARY} find "${CONTAINERS_DIR}" -name "*.img" -exec chcon u:object_r:vold_data_file:s0 {} + 2>/dev/null

# Wait for boot to complete
log "Waiting for boot to complete..."
while [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ]; do
    sleep 1
done

log "Boot completed, waiting 25 seconds for system stability..."
sleep 25

# Find all container.config files
log "Scanning for container configurations..."
CONFIG_FILES=$(${BUSYBOX_BINARY} find "${CONTAINERS_DIR}" -name "container.config" 2>/dev/null)

if [ -z "${CONFIG_FILES}" ]; then
    log "No container configs found, exiting"
    exit 0
fi

log "Found container configs, processing..."

# Process each config file
container_count=0
success_count=0
failed_count=0

for cfg in ${CONFIG_FILES}; do
    if [ ! -f "${cfg}" ]; then
        continue
    fi

    # Parse only mandatory fields for filtering/logging
    name=$(get_config_value "${cfg}" "name")
    run_at_boot=$(get_config_value "${cfg}" "run_at_boot")

    # Skip if run_at_boot is not 1
    if [ "${run_at_boot}" != "1" ]; then
        continue
    fi

    container_count=$((container_count + 1))

    # Use name for log, fall back to dirname if missing
    display_name="${name:-$(basename "$(dirname "${cfg}")")}"

    # Verify if container is already running before starting
    if [ -n "${name}" ]; then
        current_pid=$("${DROIDSPACE_BINARY}" --name="${name}" pid 2>/dev/null)
        if [ "${current_pid}" != "NONE" ]; then
            log "SKIPPING: Container '${display_name}' is already running (PID: ${current_pid})"
            success_count=$((success_count + 1))
            continue
        fi
    fi

    log "Starting container: ${display_name}"

    # Execute using the new --config flag (Binary handles all parsing/validation)
    "${DROIDSPACE_BINARY}" --config "${cfg}" start 2>&1
    exit_code=$?

    if [ ${exit_code} -eq 0 ]; then
        log "SUCCESS: Container '${display_name}' started successfully"
        success_count=$((success_count + 1))
    else
        log "FAILED: Container '${display_name}' failed to start (exit code: ${exit_code})"
        failed_count=$((failed_count + 1))
    fi
done

log "Boot auto-start summary: ${container_count} processed, ${success_count} started, ${failed_count} failed"
