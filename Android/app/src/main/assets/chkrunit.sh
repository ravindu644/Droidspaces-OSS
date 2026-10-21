#!/bin/sh
# chkrunit.sh - runit service query tool

ACTIVE_DIR=""
for dir in /etc/service /service /var/service; do
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

_all_running() {
    [ -n "$ACTIVE_DIR" ] || return 0
    for path in "$ACTIVE_DIR"/*; do
        [ -e "$path" ] || [ -L "$path" ] || continue
        [ -d "$path" ] || continue
        name=$(basename "$path")
        status=$(sv status "$path" 2>/dev/null) || status=""

        case "$status" in
            run:*)
                printf '%s|%s\n' "$name" "$status"
                ;;
        esac
    done
}

_all_enabled() {
    [ -n "$ACTIVE_DIR" ] || return 0
    for path in "$ACTIVE_DIR"/*; do
        [ -e "$path" ] || [ -L "$path" ] || continue
        [ -d "$path" ] || continue
        name=$(basename "$path")
        status=$([ -f "$path/down" ] && printf "run-at-boot: disabled" || printf "run-at-boot: enabled" )

        printf '%s|%s\n' "$name" "$status"
    done
}

_all_disabled() {
    [ -n "$ACTIVE_DIR" ] || return 0
    [ -n "$SV_DIR" ] || return 0
    for path in "$SV_DIR"/*; do
        [ -e "$path" ] || [ -L "$path" ] || continue
        [ -d "$path" ] || continue
        name=$(basename "$path")
        [ -e "$ACTIVE_DIR/$name" ] && continue
        status=$([ -f "$path/down" ] && printf "run-at-boot: disabled" || printf "run-at-boot: enabled" )

        printf '%s|%s\n' "$name" "$status"
    done
}

_all_services() {
    [ -n "$ACTIVE_DIR" ] || return 0
    [ -n "$SV_DIR" ] || return 0
    for path in "$SV_DIR"/*; do
        [ -e "$path" ] || [ -L "$path" ] || continue
        [ -d "$path" ] || continue
        name=$(basename "$path")
        status=$([ -e "$ACTIVE_DIR/$name" ] && printf "enabled" || printf "disabled" )

        printf '%s|%s\n' "$name" "$status"
    done
}

case "$1" in
    --running)
        _all_running
        ;;
    --enabled)
        _all_enabled
        ;;
    --disabled)
        _all_disabled
        ;;
    --all)
        _all_services
        ;;
esac
