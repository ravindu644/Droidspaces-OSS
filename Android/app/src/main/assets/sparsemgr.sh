#!/system/bin/sh

# sparsemgr.sh - Generic Sparse ext4 Image Manager
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Handles migration from directory-based rootfs to sparse ext4 images,
# and resizing of existing sparse images.

SCRIPT_NAME="$(basename "$0")"

# BusyBox Detection
_find_busybox() {
    if [ -x "/data/local/Droidspaces/bin/busybox" ]; then
        echo "/data/local/Droidspaces/bin/busybox"
        return
    fi
    local _p
    _p="$(command -v busybox 2>/dev/null)"
    [ -n "$_p" ] && { echo "$_p"; return; }
    _p="$(which busybox 2>/dev/null)"
    [ -n "$_p" ] && { echo "$_p"; return; }
    echo ""
}
BB=$(_find_busybox)

# Logging
log()   { echo "[SPARSE] $1"; }
warn()  { echo "[WARN]   $1"; }
error() { echo "[ERROR]  $1"; }


# Portable Tool Wrappers

# truncate -s <size> <file>
_truncate() {
    # BusyBox first for the same reason as _mount: one implementation that behaves
    # the same on every device. Both are a plain ftruncate and produce a
    # byte-identical sparse file, so there is nothing to lose by preferring ours.
    if [ -n "$BB" ] && "$BB" truncate -s "$1" "$2" 2>/dev/null; then
        return 0
    elif truncate -s "$1" "$2" 2>/dev/null; then
        return 0
    fi
    error "truncate: neither system nor busybox truncate succeeded"
    return 1
}

# mount wrapper - passes all args through, tries busybox then system
_mount() {
    # BusyBox first: Android's mount is toybox, whose losetup aborts rather than
    # truncating when a backing path exceeds 63 characters, so it cannot loop-mount
    # an image stored on a deep custom path.
    if [ -n "$BB" ] && "$BB" mount "$@" 2>/dev/null; then
        return 0
    elif mount "$@" 2>/dev/null; then
        return 0
    fi
    error "mount: failed with args: $*"
    return 1
}

# umount wrapper
_umount() {
    # Same order as _mount, so whatever attached the loop device also detaches it.
    if [ -n "$BB" ] && "$BB" umount "$@" 2>/dev/null; then
        return 0
    elif umount "$@" 2>/dev/null; then
        return 0
    fi
    return 1
}

# mountpoint check (returns 0 if mounted)
_is_mounted() {
    local path="$1"
    if [ -n "$BB" ] && "$BB" mountpoint -q "$path" 2>/dev/null; then
        return 0
    elif mountpoint -q "$path" 2>/dev/null; then
        return 0
    fi
    # Fallback: parse /proc/mounts
    grep -q " $path " /proc/mounts 2>/dev/null
}

# stat file size in bytes
_stat_size() {
    local out
    out=$(stat -c%s "$1" 2>/dev/null) && [ -n "$out" ] && { echo "$out"; return; }
    [ -n "$BB" ] && out=$("$BB" stat -c%s "$1" 2>/dev/null) && [ -n "$out" ] && { echo "$out"; return; }
    # last resort: wc
    wc -c < "$1" 2>/dev/null || echo "0"
}

# du actual disk usage
_du_h() {
    local out
    out=$(du -h "$1" 2>/dev/null | cut -f1) && [ -n "$out" ] && { echo "$out"; return; }
    [ -n "$BB" ] && out=$("$BB" du -h "$1" 2>/dev/null | cut -f1) && [ -n "$out" ] && { echo "$out"; return; }
    echo "unknown"
}

# portable sync + sleep
_sync()  { sync 2>/dev/null || { [ -n "$BB" ] && "$BB" sync 2>/dev/null; } || true; }
_sleep() { sleep "$1" 2>/dev/null || { [ -n "$BB" ] && "$BB" sleep "$1" 2>/dev/null; } || true; }

# mkdir -p wrapper
_mkdir() { mkdir -p "$@" 2>/dev/null || { [ -n "$BB" ] && "$BB" mkdir -p "$@" 2>/dev/null; }; }

# rm wrapper
_rm()    { rm "$@" 2>/dev/null || { [ -n "$BB" ] && "$BB" rm "$@" 2>/dev/null; } || true; }

# mv wrapper
_mv()    { mv "$@" 2>/dev/null || { [ -n "$BB" ] && "$BB" mv "$@" 2>/dev/null; }; }

# tar: prefer busybox tar for Android compat, fall back to system tar
_tar_create() {
    # _tar_create <source_dir>   - writes to stdout
    if [ -n "$BB" ] && "$BB" tar --help >/dev/null 2>&1; then
        (cd "$1" && "$BB" tar -cf - .)
    else
        (cd "$1" && tar -cf - .)
    fi
}
_tar_extract() {
    # _tar_extract <dest_dir>   - reads from stdin
    if [ -n "$BB" ] && "$BB" tar --help >/dev/null 2>&1; then
        (cd "$1" && "$BB" tar -xf -)
    else
        (cd "$1" && tar -xf -)
    fi
}

# mkfs.ext4 wrapper
_mkfs_ext4() {
    local img="$1"
    if command -v mkfs.ext4 >/dev/null 2>&1; then
        mkfs.ext4 -F -L "rootfs" "$img" >/dev/null
        return $?
    elif command -v mke2fs >/dev/null 2>&1; then
        mke2fs -t ext4 -F -L "rootfs" "$img" >/dev/null
        return $?
    fi
    # Deliberately no busybox fallback: its mke2fs rejects -t and builds bare ext2,
    # with no has_journal, no extent and no ext_attr, so a rootfs formatted by it
    # cannot hold SELinux labels or file capabilities. The ext4 driver would mount
    # it anyway, turning a clear failure into a subtly broken container.
    error "No ext4 formatting tool found (tried mkfs.ext4, mke2fs)"
    return 1
}

# Requirement Checks
check_migrate_requirements() {
    log "Checking tools for migrate..."
    local fail=0

    if [ -z "$BB" ]; then
        warn "busybox not found at default path or in PATH - some fallbacks unavailable"
    else
        log "busybox   : $BB"
    fi

    local has_mkfs=0
    command -v mkfs.ext4 >/dev/null 2>&1 && has_mkfs=1
    command -v mke2fs    >/dev/null 2>&1 && has_mkfs=1
    # No busybox probe here: _mkfs_ext4 deliberately does not fall back to it, so
    # counting it would report ok on a device where formatting then fails.
    if [ "$has_mkfs" -eq 0 ]; then
        error "mkfs.ext4 / mke2fs not found"
        fail=1
    else
        log "mkfs.ext4 : ok"
    fi

    local has_tar=0
    command -v tar >/dev/null 2>&1 && has_tar=1
    [ -n "$BB" ] && "$BB" tar --help >/dev/null 2>&1 && has_tar=1
    if [ "$has_tar" -eq 0 ]; then
        error "tar not found"
        fail=1
    else
        log "tar       : ok"
    fi

    [ "$fail" -ne 0 ] && exit 1
    log "All migrate requirements met"
}

check_resize_requirements() {
    log "Checking tools for resize..."
    local fail=0

    command -v e2fsck >/dev/null 2>&1    || { error "e2fsck not found";    fail=1; }
    command -v resize2fs >/dev/null 2>&1 || { error "resize2fs not found"; fail=1; }

    [ "$fail" -ne 0 ] && { error "Install e2fsprogs and retry"; exit 1; }
    log "All resize requirements met"
}

# migrate: cleanup on error
_migrate_cleanup() {
    warn "Error during migration - cleaning up temp files..."
    _is_mounted "$ROOTFS_SPARSE" && {
        _umount "$ROOTFS_SPARSE" 2>/dev/null ||
        _umount -f "$ROOTFS_SPARSE" 2>/dev/null ||
        _umount -l "$ROOTFS_SPARSE" 2>/dev/null
    }
    _rm -rf "$ROOTFS_SPARSE"
    _rm -f  "${ROOTFS_IMG}"
    log "Cleanup done - original rootfs is preserved"
}

# CMD: migrate
cmd_migrate() {
    local size_input="$1"
    local size_gb
    size_gb=$(echo "$size_input" | sed 's/[^0-9]//g')

    # Validate size
    if [ -z "$size_gb" ]; then
        error "Invalid size: '$size_input'"
        echo "Example: $SCRIPT_NAME -d /path/to/dir migrate 16"
        exit 1
    fi
    if [ "$size_gb" -lt 4 ] || [ "$size_gb" -gt 512 ]; then
        error "Size must be between 4 and 512 GB (got: ${size_gb}GB)"
        exit 1
    fi

    # Validate paths
    if [ ! -d "$ROOTFS_DIR" ]; then
        error "rootfs directory not found: $ROOTFS_DIR"
        exit 1
    fi
    if [ -z "$(ls -A "$ROOTFS_DIR" 2>/dev/null)" ]; then
        error "rootfs directory is empty: $ROOTFS_DIR"
        exit 1
    fi
    if [ -f "$ROOTFS_IMG" ]; then
        error "Sparse image already exists: $ROOTFS_IMG"
        error "Use 'resize' to change its size, or remove it manually to re-migrate"
        exit 1
    fi
    if [ -d "$ROOTFS_SPARSE" ]; then
        error "Temp migration directory already exists: $ROOTFS_SPARSE"
        error "Remove it manually before retrying"
        exit 1
    fi

    # Refuse if rootfs dir is currently mounted
    if _is_mounted "$ROOTFS_DIR"; then
        error "$ROOTFS_DIR is currently mounted"
        error "Unmount it first before running migrate"
        exit 1
    fi

    check_migrate_requirements

    log " Migration: directory -> sparse image"
    log "   Source  : $ROOTFS_DIR"
    log "   Image   : $ROOTFS_IMG"
    log "   Size    : ${size_gb}GB"

    trap _migrate_cleanup EXIT

    # Step 1: Create sparse file
    log "[1/5] Creating ${size_gb}GB sparse file..."
    if ! _truncate "${size_gb}G" "$ROOTFS_IMG"; then
        exit 1
    fi
    _sync; _sleep 2

    if [ ! -f "$ROOTFS_IMG" ]; then
        error "Sparse file was not created"
        exit 1
    fi
    local fsz
    fsz=$(_stat_size "$ROOTFS_IMG")
    if [ "$fsz" = "0" ] || [ -z "$fsz" ]; then
        error "Created file has zero size - disk full?"
        exit 1
    fi
    log "    File size: $fsz bytes ✓"

    # Step 2: Format as ext4
    log "[2/5] Formatting as ext4..."
    _sync; _sleep 1
    if ! _mkfs_ext4 "$ROOTFS_IMG"; then
        exit 1
    fi
    log "    ext4 format complete ✓"

    # Step 3: Mount sparse image to temp dir
    log "[3/5] Mounting sparse image..."
    _mkdir "$ROOTFS_SPARSE"
    chcon u:object_r:vold_data_file:s0 "$ROOTFS_IMG" 2>/dev/null || true
    if ! _mount -t ext4 -o loop,rw,noatime,nodiratime,data=ordered,commit=30 \
            "$ROOTFS_IMG" "$ROOTFS_SPARSE"; then
        exit 1
    fi
    log "    Mounted at $ROOTFS_SPARSE ✓"

    # Step 4: Copy rootfs via tar pipe
    log "[4/5] Copying rootfs (this may take a while)..."
    if ! _tar_create "$ROOTFS_DIR" | _tar_extract "$ROOTFS_SPARSE"; then
        error "tar pipe failed - rootfs copy incomplete"
        exit 1
    fi
    log "    Copy complete ✓"

    # Step 5: Finalize
    log "[5/5] Finalizing..."
    _sync
    if ! _umount "$ROOTFS_SPARSE" 2>/dev/null; then
        _umount -l "$ROOTFS_SPARSE" 2>/dev/null || {
            error "Failed to unmount sparse image"
            exit 1
        }
    fi

    local backup_dir="${ROOTFS_DIR}.bak"
    _mv "$ROOTFS_DIR" "$backup_dir"   || { error "Failed to rename original rootfs"; exit 1; }
    _mv "$ROOTFS_SPARSE" "$ROOTFS_DIR" || {
        error "Failed to rename sparse mount dir"
        _mv "$backup_dir" "$ROOTFS_DIR" 2>/dev/null
        exit 1
    }

    # NixOS marks /var/empty immutable, so not even root can delete it as it is. A mount
    # left under here would let chattr unprotect host files, so then leave the flags alone.
    { [ -z "$BB" ] || "$BB" grep -qF " $backup_dir/" /proc/mounts ||
        "$BB" chattr -R -ia "$backup_dir"; } >/dev/null 2>&1
    _rm -rf "$backup_dir"
    trap - EXIT

    log ""
    log "✅ Migration complete!"
    log "   Sparse image : $ROOTFS_IMG  (${size_gb}GB)"
    log "   Rootfs dir   : $ROOTFS_DIR"
}

# CMD: resize
cmd_resize() {
    local new_size_gb="$1"

    # Validate input
    if [ -z "$new_size_gb" ]; then
        error "No size specified"
        echo "Usage: $SCRIPT_NAME -i <image> resize <size_in_gb>"
        exit 1
    fi
    if ! [ "$new_size_gb" -eq "$new_size_gb" ] 2>/dev/null || [ "$new_size_gb" -le 0 ]; then
        error "Invalid size: '$new_size_gb' - must be a positive integer"
        exit 1
    fi
    if [ "$new_size_gb" -lt 4 ] || [ "$new_size_gb" -gt 512 ]; then
        error "Size must be between 4 and 512 GB"
        exit 1
    fi
    if [ ! -f "$RESIZE_IMG" ]; then
        error "Sparse image not found: $RESIZE_IMG"
        error "Run 'migrate' first to create a sparse image"
        exit 1
    fi

    check_resize_requirements

    # Check if the image is currently loop-mounted
    if grep -q "$RESIZE_IMG" /proc/mounts 2>/dev/null; then
        log "Image is currently mounted - unmounting..."
        local mount_point
        mount_point=$(grep "$RESIZE_IMG" /proc/mounts 2>/dev/null | awk '{print $2}' | head -1)
        if [ -n "$mount_point" ]; then
            _umount -f "$mount_point" 2>/dev/null ||
            _umount -l "$mount_point" 2>/dev/null || {
                error "Failed to unmount $mount_point - stop any processes using the image first"
                exit 1
            }
            _sleep 1
            log "    Unmounted $mount_point ✓"
        fi
    fi

    # Gather size info
    local actual_size sparse_size
    actual_size=$(_du_h "$RESIZE_IMG")
    sparse_size=$(ls -lh "$RESIZE_IMG" 2>/dev/null | tr -s ' ' | cut -d' ' -f5)
    [ -z "$sparse_size" ] && sparse_size="unknown"

    # No minimum or truncate of our own. resize2fs computes the real minimum from
    # the fs metadata and refuses below it (we never pass -f), and on an image
    # file it extends or ftruncates the file itself.

    log " Resize: $RESIZE_IMG"
    echo "   Image size           : $sparse_size"
    echo "   Allocated on disk    : $actual_size"
    echo "   Requested size       : ${new_size_gb}G"

    # Confirmation (skipped with -y / --yes)
    if [ "$YES_FLAG" -eq 0 ]; then
        warn "WARNING: Resizing a sparse image is a destructive operation"
        warn "  that CAN CORRUPT your filesystem if interrupted."
        warn "  Make a FULL BACKUP before proceeding."
        warn ""
        warn "  Resize : $sparse_size -> ${new_size_gb}G"
        printf "  Type 'YES' to confirm: "
        read -r _confirm
        [ "$_confirm" != "YES" ] && { log "Resize cancelled"; exit 0; }
    fi

    # Filesystem check
    log "[1/3] Checking filesystem integrity..."
    local fsck_out fsck_exit
    fsck_out=$(e2fsck -f -y "$RESIZE_IMG" 2>&1 >/dev/null)
    fsck_exit=$?
    # e2fsck exit codes: 0=clean, 1=corrected, 2=corrected+reboot, 4+=error
    if [ $fsck_exit -ge 4 ]; then
        error "Filesystem check FAILED (exit: $fsck_exit)"
        error "$fsck_out"
        error "Do NOT proceed - restore from backup"
        exit 1
    fi
    [ $fsck_exit -ne 0 ] && warn "e2fsck corrected issues (exit: $fsck_exit) - continuing"
    [ $fsck_exit -eq 0 ] && log "    Filesystem clean ✓"

    # Resize filesystem
    log "[2/3] Resizing filesystem to ${new_size_gb}G..."
    local resize_out resize_exit
    resize_out=$(resize2fs "$RESIZE_IMG" "${new_size_gb}G" 2>&1 >/dev/null)
    resize_exit=$?
    # resize2fs may exit non-zero but print "is now N blocks long" which means success
    if [ $resize_exit -ne 0 ] && ! echo "$resize_out" | grep -q "is now.*blocks long"; then
        error "resize2fs FAILED (exit: $resize_exit)"
        error "$resize_out"
        # resize2fs checks its minimum before touching anything, so this one is harmless
        if echo "$resize_out" | grep -q "smaller than minimum"; then
            error "Too small for the data in this image. Nothing was changed, pick a larger size."
            exit 1
        fi
        error "Restore from backup immediately!"
        exit 1
    fi
    [ $resize_exit -ne 0 ] && warn "resize2fs exited $resize_exit but reported success - continuing"
    log "    Filesystem resized ✓"

    # Verify by test-mounting to a temp directory
    log "[3/3] Verifying resized filesystem..."
    local verify_dir
    verify_dir="$(dirname "$RESIZE_IMG")/.resize_verify_$$"
    _mkdir "$verify_dir"
    if _mount -t ext4 -o loop,ro "$RESIZE_IMG" "$verify_dir" 2>/dev/null; then
        _umount "$verify_dir" 2>/dev/null
        _rm -rf "$verify_dir"
        log "    Mount verification passed ✓"
    else
        _rm -rf "$verify_dir"
        error "Failed to mount resized filesystem - possible corruption!"
        error "Restore from backup immediately!"
        exit 1
    fi

    _sleep 1
    local new_sparse
    new_sparse=$(ls -lh "$RESIZE_IMG" 2>/dev/null | tr -s ' ' | cut -d' ' -f5)

    log ""
    log "Resize complete!"
    log "   $sparse_size -> ${new_sparse}"
    log "   Image: $RESIZE_IMG"
}


# Usage

usage() {
    echo "Usage:"
    echo "  $SCRIPT_NAME -d <base_dir> migrate <size_gb>"
    echo "  $SCRIPT_NAME -i <image>    resize  <size_gb>"
    echo ""
    echo "Options:"
    echo "  -d, --dir <path>      Base directory containing rootfs/ (for migrate)"
    echo "                        Derives rootfs/ and rootfs.img from this path"
    echo "  -i, --image <file>    Path to an existing sparse image (for resize)"
    echo "  -y, --yes             Skip confirmation prompts"
    echo "  -h, --help            Show this help and exit"
    echo ""
    echo "Commands:"
    echo "  migrate <size_gb>     Migrate directory-based rootfs/ to a sparse ext4 image"
    echo "                        size: 4-512 GB"
    echo "  resize  <size_gb>     Resize an existing sparse image"
    echo "                        size: 4-512 GB"
    echo ""
    echo "BusyBox:"
    echo "  Default path  : /data/local/Droidspaces/bin/busybox"
    echo "  Auto-fallback : command -v busybox, then which busybox"
    if [ -n "$BB" ]; then
        echo "  Detected      : $BB"
    else
        echo "  Detected      : NOT FOUND (some operations may fail)"
    fi
    echo ""
    echo "Examples:"
    echo "  $SCRIPT_NAME -d /data/local/ubuntu migrate 16"
    echo "  $SCRIPT_NAME -i /data/local/ubuntu/rootfs.img resize 32"
    echo "  $SCRIPT_NAME -i /data/local/ubuntu/rootfs.img resize 8 --yes"
    echo ""
    echo "Notes:"
    echo "  - Unmount the image/rootfs before running either command."
    echo "  - Always back up before resizing."
    echo "  - migrate requires: mkfs.ext4 (or mke2fs), tar"
    echo "  - resize  requires: e2fsck, resize2fs"
    exit "${1:-0}"
}


# Argument Parsing

BASE_DIR=""
RESIZE_IMG=""
COMMAND=""
CMD_ARG=""
YES_FLAG=0

while [ $# -gt 0 ]; do
    case "$1" in
        -d|--dir)
            [ -z "$2" ] && { error "--dir requires a path argument"; exit 1; }
            BASE_DIR="$2"; shift 2 ;;
        -i|--image)
            [ -z "$2" ] && { error "--image requires a file path argument"; exit 1; }
            RESIZE_IMG="$2"; shift 2 ;;
        -y|--yes)
            YES_FLAG=1; shift ;;
        -h|--help)
            usage 0 ;;
        migrate|resize)
            COMMAND="$1"
            shift
            # Grab the size argument (next positional, if not a flag)
            if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then
                CMD_ARG="$1"; shift
            fi
            ;;
        -*)
            error "Unknown option: $1"
            usage 1 ;;
        *)
            # Bare positional: treat as size argument if command already set
            if [ -n "$COMMAND" ] && [ -z "$CMD_ARG" ]; then
                CMD_ARG="$1"; shift
            else
                error "Unexpected argument: $1"
                usage 1
            fi
            ;;
    esac
done


# Validate Required Args

if [ -z "$COMMAND" ]; then
    error "No command specified"
    usage 1
fi

case "$COMMAND" in
    migrate)
        if [ -z "$BASE_DIR" ]; then
            error "migrate requires -d <base_dir>"
            usage 1
        fi
        ;;
    resize)
        if [ -z "$RESIZE_IMG" ]; then
            error "resize requires -i <image>"
            usage 1
        fi
        ;;
esac

if [ -z "$CMD_ARG" ]; then
    error "Size argument required (e.g. '$SCRIPT_NAME ... $COMMAND 16')"
    usage 1
fi


# Root Check

if [ "$(id -u)" -ne 0 ]; then
    error "This script must be run as root"
    exit 1
fi


# Derive Paths (migrate only)

if [ "$COMMAND" = "migrate" ]; then
    if [ ! -d "$BASE_DIR" ]; then
        error "Base directory does not exist: $BASE_DIR"
        exit 1
    fi
    ROOTFS_DIR="$BASE_DIR/rootfs"
    ROOTFS_IMG="$BASE_DIR/rootfs.img"
    ROOTFS_SPARSE="$BASE_DIR/rootfs.sparse"   # temp mount point during migration
fi

# Dispatch

case "$COMMAND" in
    migrate) cmd_migrate "$CMD_ARG" ;;
    resize)  cmd_resize  "$CMD_ARG" ;;
esac
