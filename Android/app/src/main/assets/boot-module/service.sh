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

# Function to quote a value for shell commands
quote_value() {
    local value="$1"
    value=$(echo "${value}" | ${BUSYBOX_BINARY} sed "s/'/'\\\\''/g" 2>/dev/null)
    echo "'${value}'"
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

# Check if containers directory exists
if [ ! -d "${CONTAINERS_DIR}" ]; then
    log "ERROR: Containers directory not found at ${CONTAINERS_DIR}"
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

    # Parse config values
    name=$(get_config_value "${cfg}" "name")
    hostname=$(get_config_value "${cfg}" "hostname")
    rootfs_path=$(get_config_value "${cfg}" "rootfs_path")
    use_sparse_image=$(get_config_value "${cfg}" "use_sparse_image")
    enable_ipv6=$(get_config_value "${cfg}" "enable_ipv6")
    enable_android_storage=$(get_config_value "${cfg}" "enable_android_storage")
    enable_hw_access=$(get_config_value "${cfg}" "enable_hw_access")
    selinux_permissive=$(get_config_value "${cfg}" "selinux_permissive")
    volatile_mode=$(get_config_value "${cfg}" "volatile_mode")
    bind_mounts=$(get_config_value "${cfg}" "bind_mounts")
    dns_servers=$(get_config_value "${cfg}" "dns_servers")
    run_at_boot=$(get_config_value "${cfg}" "run_at_boot")

    # Skip if run_at_boot is not 1
    if [ "${run_at_boot}" != "1" ]; then
        continue
    fi

    container_count=$((container_count + 1))

    # Validate required fields
    if [ -z "${name}" ] || [ -z "${rootfs_path}" ]; then
        log "WARNING: Skipping invalid config: ${cfg} (missing name or rootfs_path)"
        failed_count=$((failed_count + 1))
        continue
    fi

    # Check if rootfs path exists (directory for regular, file for sparse image)
    if [ "${use_sparse_image}" = "1" ]; then
        # Sparse image: check if file exists
        if [ ! -f "${rootfs_path}" ]; then
            log "WARNING: Skipping container '${name}': sparse image does not exist: ${rootfs_path}"
            failed_count=$((failed_count + 1))
            continue
        fi
    else
        # Regular rootfs: check if directory exists
    if [ ! -d "${rootfs_path}" ]; then
        log "WARNING: Skipping container '${name}': rootfs path does not exist: ${rootfs_path}"
        failed_count=$((failed_count + 1))
        continue
        fi
    fi

    log "Processing container: ${name}"

    # Build droidspaces command
    cmd="${DROIDSPACE_BINARY}"

    # Add --name (quoted to handle spaces)
    cmd="${cmd} --name=$(quote_value "${name}")"

    # Add --rootfs or --rootfs-img based on sparse image setting
    if [ "${use_sparse_image}" = "1" ]; then
        cmd="${cmd} --rootfs-img=$(quote_value "${rootfs_path}")"
    else
    cmd="${cmd} --rootfs=$(quote_value "${rootfs_path}")"
    fi

    # Add --hostname if defined (Always pass to prevent conflicts with auto-naming)
    if [ -n "${hostname}" ]; then
        cmd="${cmd} --hostname=$(quote_value "${hostname}")"
    fi

    # Add --dns if defined
    if [ -n "${dns_servers}" ]; then
        cmd="${cmd} --dns=$(quote_value "${dns_servers}")"
    fi

    # Add --bind-mount if defined
    if [ -n "${bind_mounts}" ]; then
        cmd="${cmd} --bind-mount=$(quote_value "${bind_mounts}")"
    fi

    # Add feature flags
    if [ "${enable_ipv6}" = "1" ]; then
        cmd="${cmd} --enable-ipv6"
    fi

    if [ "${enable_android_storage}" = "1" ]; then
        cmd="${cmd} --enable-android-storage"
    fi

    if [ "${enable_hw_access}" = "1" ]; then
        cmd="${cmd} --hw-access"
    fi

    if [ "${selinux_permissive}" = "1" ]; then
        cmd="${cmd} --selinux-permissive"
    fi

    if [ "${volatile_mode}" = "1" ]; then
        cmd="${cmd} --volatile"
    fi

    # Add start command
    cmd="${cmd} start"

    # Execute command
    log "Starting container: ${name}"
    eval "${cmd}" 2>&1
    exit_code=$?

    if [ ${exit_code} -eq 0 ]; then
        log "SUCCESS: Container '${name}' started successfully"
        success_count=$((success_count + 1))
    else
        log "FAILED: Container '${name}' failed to start (exit code: ${exit_code})"
        failed_count=$((failed_count + 1))
    fi
done

log "Boot auto-start summary: ${container_count} processed, ${success_count} started, ${failed_count} failed"
