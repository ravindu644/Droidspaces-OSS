#!/system/bin/sh
# export_container.sh - Export a Droidspaces container as a .tar.gz archive
# Copyright (c) 2026 ravindu644
#
# Usage: export_container.sh <container_name> <output_path> [--with-env]
#
# --with-env also packs the container's .env as container.env. It is opt-in
# because .env files tend to hold passwords and tokens.
#
# Works in both sparse image (rootfs.img) and directory-based modes.
# In sparse mode: mounts rootfs.img read-only, tars from mount point, unmounts.
# In directory mode: tars directly from rootfs/.

set -e

DS_PATH="/data/local/Droidspaces"
CONTAINERS_BASE_PATH="${DS_PATH}/Containers"
BUSYBOX_PATH="${DS_PATH}/bin/busybox"

# --- Logging ---
log()   { echo "[INFO] $1"; }
error() { echo "[ERROR] $1"; }

# --- Cleanup state ---
TEMP_MOUNT=""
TEMP_CONFIG_DIR=""
CLEANUP_DONE=0

cleanup() {
    if [ "$CLEANUP_DONE" -eq 1 ]; then return; fi
    CLEANUP_DONE=1

    # BusyBox first throughout, matching the mount below: whatever attached the
    # loop device should be what detaches it. $BUSYBOX is empty until it is
    # resolved further down, and the guards fall through to the system tools then.
    if [ -n "$TEMP_MOUNT" ] && { { [ -n "$BUSYBOX" ] && "$BUSYBOX" mountpoint -q "$TEMP_MOUNT" 2>/dev/null; } \
        || mountpoint -q "$TEMP_MOUNT" 2>/dev/null; }; then
        log "Unmounting temporary mount point..."
        { [ -n "$BUSYBOX" ] && "$BUSYBOX" umount -f "$TEMP_MOUNT" 2>/dev/null; } \
            || { [ -n "$BUSYBOX" ] && "$BUSYBOX" umount -l "$TEMP_MOUNT" 2>/dev/null; } \
            || umount -f "$TEMP_MOUNT" 2>/dev/null \
            || umount -l "$TEMP_MOUNT" 2>/dev/null || true
    fi

    if [ -n "$TEMP_MOUNT" ] && [ -d "$TEMP_MOUNT" ]; then
        rmdir "$TEMP_MOUNT" 2>/dev/null || true
    fi
    if [ -n "$TEMP_CONFIG_DIR" ]; then
        "$BUSYBOX" rm -rf "$TEMP_CONFIG_DIR"
    fi
}

# Always cleanup on exit (covers both success and failure)
trap cleanup EXIT
trap 'error "Interrupted."; exit 1' INT TERM

# --- Validate busybox ---
if [ -x "$BUSYBOX_PATH" ]; then
    BUSYBOX="$BUSYBOX_PATH"
elif command -v busybox >/dev/null 2>&1; then
    BUSYBOX="busybox"
    log "System busybox at fallback path: $(command -v busybox)"
else
    error "busybox not found at $BUSYBOX_PATH and not in PATH. Aborting."
    exit 1
fi

# --- Argument validation ---
if [ $# -lt 2 ] || [ $# -gt 3 ] || { [ $# -eq 3 ] && [ "$3" != "--with-env" ]; }; then
    error "Usage: export_container.sh <container_name> <output_path> [--with-env]"
    exit 1
fi

CONTAINER_NAME="$1"
OUTPUT_PATH="$2"
WITH_ENV="$3"

if [ -z "$CONTAINER_NAME" ]; then
    error "Container name cannot be empty."
    exit 1
fi

if [ -z "$OUTPUT_PATH" ]; then
    error "Output path cannot be empty."
    exit 1
fi

# --- Resolve container paths ---
# Sanitize name: match directory convention (spaces -> hyphens)
CONTAINER_DIR_NAME=$(echo "$CONTAINER_NAME" | tr ' ' '-')
CONTAINER_DIR="${CONTAINERS_BASE_PATH}/${CONTAINER_DIR_NAME}"
CONFIG_FILE="${CONTAINER_DIR}/container.config"

if [ ! -d "$CONTAINER_DIR" ]; then
    error "Container directory not found: $CONTAINER_DIR"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    error "container.config not found at $CONFIG_FILE"
    exit 1
fi

# Snapshot the current host config, including edits made since this rootfs was imported.
TEMP_CONFIG_DIR=$("$BUSYBOX" mktemp -d "${CONTAINER_DIR}/.export-config.XXXXXX")
"$BUSYBOX" cp "$CONFIG_FILE" "$TEMP_CONFIG_DIR/container.config"
ROOTFS_PATH=$("$BUSYBOX" sed -n 's/^rootfs_path=//p' "$TEMP_CONFIG_DIR/container.config")

if [ -z "$ROOTFS_PATH" ]; then
    error "Could not extract rootfs_path from $CONFIG_FILE"
    exit 1
fi

# The config names the .env it uses, and the CLI can point that anywhere.
HEADER_MEMBERS="container.config"
if [ -n "$WITH_ENV" ]; then
    ENV_FILE=$("$BUSYBOX" sed -n 's/^env_file=//p' "$TEMP_CONFIG_DIR/container.config")
    if [ -n "$ENV_FILE" ] && [ -f "$ENV_FILE" ]; then
        "$BUSYBOX" cp "$ENV_FILE" "$TEMP_CONFIG_DIR/container.env"
        HEADER_MEMBERS="container.config container.env"
        log "Including environment variables from $ENV_FILE"
    else
        log "No environment file to include"
    fi
fi

# --- Detect mode ---
if echo "$ROOTFS_PATH" | grep -q "\.img$"; then
    MODE="sparse"
    ROOTFS_IMG="$ROOTFS_PATH"
else
    MODE="directory"
    ROOTFS_DIR="$ROOTFS_PATH"
fi

log "Container: $CONTAINER_NAME (Directory: $CONTAINER_DIR_NAME)"
log "Mode: $MODE"
log "Rootfs: $ROOTFS_PATH"
log "Output: $OUTPUT_PATH"

# --- Ensure output directory exists ---
OUTPUT_DIR="$(dirname "$OUTPUT_PATH")"
if [ ! -d "$OUTPUT_DIR" ]; then
    error "Output directory does not exist: $OUTPUT_DIR"
    exit 1
fi

# --- Execute export ---
if [ "$MODE" = "sparse" ]; then
    log "Sparse image detected: $ROOTFS_IMG"

    if [ ! -f "$ROOTFS_IMG" ]; then
        error "Sparse image file not found: $ROOTFS_IMG"
        exit 1
    fi

    # Filesystem check before mounting
    log "Checking filesystem integrity..."
    FSCK_OUT=$(e2fsck -f -y "$ROOTFS_IMG" 2>&1) || true
    FSCK_EXIT=$?

    if [ "$FSCK_EXIT" -ge 4 ]; then
        error "Filesystem check failed (exit: $FSCK_EXIT)"
        error "$FSCK_OUT"
        error "Filesystem corruption detected - cannot export safely."
        exit 1
    elif [ "$FSCK_EXIT" -ne 0 ]; then
        log "Filesystem check corrected some issues (exit: $FSCK_EXIT). Continuing."
    else
        log "Filesystem integrity verified."
    fi

    sleep 1

    # Create temp mount point
    TEMP_MOUNT="${CONTAINER_DIR}/_export_mnt_$$"
    mkdir -p "$TEMP_MOUNT" || { error "Failed to create temporary mount point."; exit 1; }

    log "Mounting sparse image read-only..."
    # Fix SELinux context before mounting to avoid permission issues
    chcon u:object_r:vold_data_file:s0 "$ROOTFS_IMG" 2>/dev/null || true

    # BusyBox first, for the same reason as sparsemgr: toybox's losetup refuses a
    # backing path over 63 characters instead of truncating the name it stores.
    if ! { [ -n "$BUSYBOX" ] && "$BUSYBOX" mount -t ext4 -o loop,ro "$ROOTFS_IMG" "$TEMP_MOUNT" 2>/dev/null; } \
       && ! mount -t ext4 -o loop,ro "$ROOTFS_IMG" "$TEMP_MOUNT" 2>/dev/null; then
        error "Failed to mount sparse image at $TEMP_MOUNT"
        exit 1
    fi
    log "Sparse image mounted at $TEMP_MOUNT"

    TAR_ROOT="$TEMP_MOUNT"
else
    # Directory mode
    if [ ! -d "$ROOTFS_DIR" ]; then
        error "rootfs directory not found: $ROOTFS_DIR"
        exit 1
    fi
    log "Directory-based container at: $ROOTFS_DIR"
    TAR_ROOT="$ROOTFS_DIR"
fi

# --- Create archive ---
log "Creating archive... (this may take a while)"
# BusyBox applies only the last -C, so prepend a small header tar without its end
# blocks. Both rootfs modes stay read-only, and no uncompressed rootfs copy is needed.
# HEADER_MEMBERS is word-split on purpose, both names are fixed and have no spaces.
"$BUSYBOX" tar -cf "$TEMP_CONFIG_DIR/header.tar" -C "$TEMP_CONFIG_DIR" $HEADER_MEMBERS
HEADER_BLOCKS=0
for member in $HEADER_MEMBERS; do
    size=$("$BUSYBOX" stat -c %s "$TEMP_CONFIG_DIR/$member")
    HEADER_BLOCKS=$((HEADER_BLOCKS + 1 + (size + 511) / 512))
done
if ! (
    set -o pipefail
    {
        "$BUSYBOX" dd if="$TEMP_CONFIG_DIR/header.tar" bs=512 count="$HEADER_BLOCKS" 2>/dev/null &&
        "$BUSYBOX" tar -cf - --exclude='./container.config' --exclude='./container.env' -C "$TAR_ROOT" .
    } | "$BUSYBOX" gzip > "$OUTPUT_PATH"
); then
    error "tar failed. Removing incomplete archive."
    rm -f "$OUTPUT_PATH" 2>/dev/null || true
    exit 1
fi

# --- Verify archive is non-empty ---
if [ ! -s "$OUTPUT_PATH" ]; then
    error "Archive is empty or was not created. Removing."
    rm -f "$OUTPUT_PATH" 2>/dev/null || true
    exit 1
fi

ARCHIVE_SIZE=$(du -h "$OUTPUT_PATH" 2>/dev/null | cut -f1)
log "Export completed successfully!"
log "Archive: $OUTPUT_PATH (${ARCHIVE_SIZE:-unknown size})"
