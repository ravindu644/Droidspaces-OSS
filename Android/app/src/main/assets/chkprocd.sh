#!/bin/sh
# chkprocd.sh - OpenWrt/procd service query tool
# Output format for --all:
#   name|description|enabled|running
# where enabled is enabled/disabled and running is running/stopped/unknown.

INITD_DIR="/etc/init.d"

_is_safe_name() {
    echo "$1" | grep -Eq '^[A-Za-z0-9_.+@-]+$'
}

_all_services() {
    [ -d "$INITD_DIR" ] || return 0
    for path in "$INITD_DIR"/*; do
        [ -f "$path" ] || continue
        [ -x "$path" ] || continue
        name="${path##*/}"
        _is_safe_name "$name" || continue
        case "$name" in
            .*) continue ;;
        esac
        echo "$name"
    done | sort -u
}

_get_desc() {
    svc="$1"
    script="$INITD_DIR/$svc"

    # Some init scripts define DESCRIPTION/description. Most OpenWrt scripts do
    # not, so empty description is a valid result.
    # Keep this parser deliberately conservative to avoid shell-quoting
    # complexity on BusyBox ash. Descriptions are optional in the UI.
    grep -m1 -E '^(DESCRIPTION|description)=' "$script" 2>/dev/null \
        | sed 's/^[^=]*=//;s/^"//;s/"$//' \
        | head -n 1
}

_is_enabled() {
    "$INITD_DIR/$1" enabled >/dev/null 2>&1
}

_running_state() {
    svc="$1"

    if "$INITD_DIR/$svc" running >/dev/null 2>&1; then
        echo running
        return 0
    fi

    # procd init scripts generally implement status; one-shot/boot scripts may
    # not. Treat unsupported status as unknown rather than pretending stopped.
    output=$("$INITD_DIR/$svc" status 2>&1)
    rc=$?
    if [ $rc -eq 0 ]; then
        echo running
        return 0
    fi

    echo "$output" | grep -Eiq 'inactive|not running|stopped|disabled|dead' && {
        echo stopped
        return 0
    }

    echo unknown
}

case "$1" in
    --all)
        for svc in $(_all_services); do
            desc=$(_get_desc "$svc")
            if _is_enabled "$svc"; then
                enabled="enabled"
            else
                enabled="disabled"
            fi
            running=$(_running_state "$svc")
            echo "${svc}|${desc}|${enabled}|${running}"
        done
        ;;
    *)
        echo "usage: $0 --all" >&2
        exit 2
        ;;
esac
