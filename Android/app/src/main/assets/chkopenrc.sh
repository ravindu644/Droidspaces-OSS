#!/bin/sh
# chkopenrc.sh - OpenRC service query tool (output format mirrors chksystemd.sh)

RUNLEVELS_DIR="/etc/runlevels"
INITD_DIR="/etc/init.d"

_all_running() {
   rc-status -f ini 2>/dev/null | awk -F'=' '/=/ {gsub(/^[ \t]+|[ \t]+$/, "", $1); print $1}'
}

_all_services() {
    ls "$INITD_DIR" 2>/dev/null | grep -v '^\.' | sort
}

_all_enabled() {
    find "$RUNLEVELS_DIR" -maxdepth 2 -type l 2>/dev/null \
        | xargs -I{} basename {} 2>/dev/null \
        | sort -u
}

_get_desc() {
    # Extract the 'description' variable from the init script
    grep -m1 '^description=' "$INITD_DIR/$1" 2>/dev/null \
        | sed 's/^description=["'\'']\{0,1\}//;s/["'\'']\{0,1\}$//'
}

case "$1" in
    --running)
        # Output: name|Description (matches chksystemd.sh --running)

        if running=$(_all_running 2>/dev/null); then
            # Preferred method: use rc-status output directly.
            echo "$running" | while IFS= read -r svc; do
                [ -n "$svc" ] || continue
                desc=$(_get_desc "$svc")
                echo "${svc}|${desc}"
            done
        else
            # Fallback: query each service individually.
            for svc in $(_all_services); do
                if rc-service "$svc" status >/dev/null 2>&1; then
                    desc=$(_get_desc "$svc")
                    echo "${svc}|${desc}"
                fi
            done
        fi
        ;;
    --enabled)
        # Output: name
        _all_enabled
        ;;
    --disabled)
        # Output: name
        enabled=$(_all_enabled)
        for svc in $(_all_services); do
            echo "$enabled" | grep -qx "$svc" || echo "$svc"
        done
        ;;
    --all)
        # Output: name|state  (enabled/disabled)
        enabled=$(_all_enabled)
        for svc in $(_all_services); do
            if echo "$enabled" | grep -qx "$svc"; then
                echo "${svc}|enabled"
            else
                echo "${svc}|disabled"
            fi
        done
        ;;
esac
