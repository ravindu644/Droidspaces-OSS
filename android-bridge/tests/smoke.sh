#!/bin/sh
# Smoke test for android-bridge on a Linux host, run as root.
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The daemon always runs /system/bin/sh. On a desktop that path does not
# exist, so link it to /bin/sh first, on a machine where that is acceptable.

BIN=$(realpath "${1:-out/android-bridge}")
[ "$(id -u)" = 0 ] || { echo "smoke: needs root"; exit 1; }
[ -x /system/bin/sh ] || { echo "smoke: needs /system/bin/sh (ln -s /bin/sh /system/bin/sh)"; exit 1; }

T=$(mktemp -d)
DIR=$T/run
SOCK=$DIR/bridge.sock
fails=0
trap 'kill $DPID 2>/dev/null; rm -rf "$T"' EXIT

"$BIN" --daemon --foreground --dir "$DIR" 2>"$T/log" &
DPID=$!
i=0
while [ ! -S "$SOCK" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done

# The shell gets Android's PATH, which has no /bin on a desktop.
P='export PATH=/usr/bin:/bin:$PATH;'
c() { "$BIN" -s "$SOCK" "$P" "$@"; }
check() {
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fails=$((fails + 1)); fi
}

check "socket and directory are root only" "$(stat -c %a "$DIR") $(stat -c %a "$SOCK")" "700 600"
check "shell runs as the shell user" "$(c id -u):$(c id -g)" "2000:2000"
check "no capabilities left" "$(c 'grep CapEff /proc/self/status' | tr -d '\t')" "CapEff:0000000000000000"
c 'exit 7'; check "exit code is passed through" "$?" "7"
c 'kill -9 $$'; check "death by signal is 128+sig" "$?" "137"
check "stdin reaches the shell" "$(printf hello | c cat)" "hello"
check "stderr stays stderr" "$(c 'echo out; echo err >&2' 2>/dev/null)" "out"
check "arguments join like adb" "$(c echo 'a  b' c)" "a b c"
check "a pty when asked" "$("$BIN" -s "$SOCK" -t "$P" tty </dev/null | tr -d '\r' | cut -c1-9)" "/dev/pts/"
check "no pty by default" "$(c tty </dev/null)" "not a tty"

head -c 8000000 /dev/urandom > "$T/big"
check "8 MB both ways without a deadlock" "$(c cat <"$T/big" | md5sum)" "$(md5sum <"$T/big")"

"$BIN" -s "$SOCK" "$P" sleep 271828 & CPID=$!
sleep 0.5; kill $CPID; wait $CPID 2>/dev/null; sleep 0.5
check "client hangup ends the shell" "$(pgrep -f '^sleep 2718' | wc -l)" "0"

out=$(setpriv --reuid=1000 --regid=1000 --clear-groups "$BIN" -s "$SOCK" true 2>&1)
check "non-root stopped by file permissions ($?)" "$out" "android-bridge: permission denied, only root may use the bridge"

# Open the file permissions so the daemon's own check is the only gate left.
chmod 755 "$T" "$DIR"; chmod 666 "$SOCK"
out=$(setpriv --reuid=1000 --regid=1000 --clear-groups "$BIN" -s "$SOCK" true 2>&1)
check "non-root stopped by the daemon ($?)" "$out" "android-bridge: permission denied, only real root may connect"
if unshare -U -r true 2>/dev/null; then
    out=$(unshare -U -r "$BIN" -s "$SOCK" true 2>&1)
    check "user namespace root stopped by the daemon ($?)" "$out" "android-bridge: permission denied, only real root may connect"
else
    echo "skip user namespace root, unshare -U is not allowed here"
fi
chmod 700 "$DIR"; chmod 600 "$SOCK"

"$BIN" --daemon --foreground --dir "$DIR" 2>/dev/null
check "a second daemon leaves the first alone" "$(c echo still)" "still"

kill $DPID; wait $DPID 2>/dev/null
"$BIN" --daemon --foreground --dir "$DIR" 2>/dev/null & DPID=$!
sleep 0.3
check "a stale socket from a crash is replaced" "$(c echo back)" "back"

echo "--- daemon log"; cat "$T/log"
[ $fails = 0 ] && echo "all passed" || { echo "$fails failed"; exit 1; }
