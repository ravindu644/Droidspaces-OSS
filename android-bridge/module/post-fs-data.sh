#!/system/bin/sh
# Starts the android-bridge daemon before Droidspaces boots any container, so
# the directory a container bind-mounts already exists.
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

MODDIR=${0%/*}
LOG=/data/adb/android-bridge.log

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.old"

# The daemon detaches once its socket is bound, so post-fs-data is not held up.
"$MODDIR/android-bridge" --daemon >>"$LOG" 2>&1

# Containers run the client from the same directory they get the socket from.
cp -f "$MODDIR/android-bridge" /dev/android-bridge/android-bridge &&
    chmod 0755 /dev/android-bridge/android-bridge
