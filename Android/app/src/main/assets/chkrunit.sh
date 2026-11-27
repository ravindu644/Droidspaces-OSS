#!/bin/sh
# chkrunit.sh - runit service query and management tool.
# Every query prints one "name|status" line per service.
#
# "enabled" means linked into the active directory that runsvdir scans.
# "run-at-boot" is the absence of a down file in the service directory.

ACTIVE_DIR=""
for dir in /etc/service /service /var/service /run/runit/service; do
    if [ -d "$dir" ]; then
        ACTIVE_DIR="$dir"
        break
    fi
done

SV_DIR=""
for dir in /etc/sv /etc/runit/sv /usr/local/etc/sv; do
    if [ -d "$dir" ]; then
        SV_DIR="$dir"
        break
    fi
done

die() {
    echo "$1" >&2
    exit 1
}

_boot_state() {
    if [ -f "$1/down" ]; then printf 'run-at-boot: disabled'; else printf 'run-at-boot: enabled'; fi
}

_all_running() {
    [ -n "$ACTIVE_DIR" ] || return 0
    for path in "$ACTIVE_DIR"/*; do
        [ -d "$path" ] || continue
        name=$(basename "$path")
        # Relative name under SVDIR, so the status line reads "run: sshd: (pid 42) 10s"
        # instead of carrying the full path.
        status=$(SVDIR="$ACTIVE_DIR" sv status "$name" 2>/dev/null) || continue
        case "$status" in
            run:*) printf '%s|%s\n' "$name" "$status" ;;
        esac
    done
}

_all_enabled() {
    [ -n "$ACTIVE_DIR" ] || return 0
    for path in "$ACTIVE_DIR"/*; do
        [ -d "$path" ] || continue
        printf '%s|%s\n' "$(basename "$path")" "$(_boot_state "$path")"
    done
}

_all_disabled() {
    [ -n "$SV_DIR" ] || return 0
    for path in "$SV_DIR"/*; do
        [ -d "$path" ] || continue
        name=$(basename "$path")
        [ -n "$ACTIVE_DIR" ] && [ -e "$ACTIVE_DIR/$name" ] && continue
        printf '%s|%s\n' "$name" "$(_boot_state "$path")"
    done
}

# Every sv call takes the absolute service directory: sv's compiled-in SVDIR
# may differ from the directory this script found, and a name is then never
# mistaken for an option.
case "$1" in
    --running)  _all_running ;;
    --enabled)  _all_enabled ;;
    --disabled) _all_disabled ;;
    --sv)
        [ -n "$ACTIVE_DIR" ] || die "no active service directory, is runsvdir running?"
        sv "$2" "$ACTIVE_DIR/$3"
        ;;
    --enable)
        [ -n "$ACTIVE_DIR" ] || die "no active service directory, is runsvdir running?"
        [ -d "$SV_DIR/$2" ] || die "no service directory for $2"
        # -f replaces a dangling link left behind by a reinstalled service
        ln -sfn "$SV_DIR/$2" "$ACTIVE_DIR/$2"
        ;;
    --disable)
        # Only a symlink can be unlinked. A real directory under ACTIVE_DIR
        # stays enabled, so refuse before stopping anything.
        [ -L "$ACTIVE_DIR/$2" ] || die "$2 is not a symlink in $ACTIVE_DIR"
        sv down "$ACTIVE_DIR/$2"
        rm "$ACTIVE_DIR/$2"
        ;;
    --toggle-boot)
        # runsv reads the down file through the active entry, so toggle it
        # there and fall back to the sv directory for a disabled service.
        dir="$ACTIVE_DIR/$2"
        [ -d "$dir" ] || dir="$SV_DIR/$2"
        [ -d "$dir" ] || die "no service directory for $2"
        if [ -f "$dir/down" ]; then rm "$dir/down"; else touch "$dir/down"; fi
        ;;
esac
