/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_TERMUX_H
#define DS_ANDROID_TERMUX_H

/* X11 Display Number - change this one value to relocate everything */
#define TX11_DISPLAY_NUM 5
#define _DS_STR(x) #x
#define _DS_XSTR(x) _DS_STR(x)
#define TX11_DISPLAY_STR ":" _DS_XSTR(TX11_DISPLAY_NUM)
#define TX11_DISPLAY_SOCK "X" _DS_XSTR(TX11_DISPLAY_NUM)

/* X11 Socket Paths (Host-side relative to /.old_root or absolute) */
#define DS_X11_PATH_DESKTOP "/.old_root/tmp/.X11-unix"
#define DS_TERMUX_TMP_OLDROOT "/.old_root/data/data/com.termux/files/usr/tmp"
#define DS_X11_CONTAINER_DIR "/tmp/.X11-unix"

/* Termux paths */
#define TX11_DATA_DIR "/data/user/0/com.termux"
#define TX11_DATA_ALT "/data/data/com.termux"
#define TX11_PREFIX "/data/data/com.termux/files/usr"
#define TX11_HOME "/data/data/com.termux/files/home"
#define TX11_LOADER TX11_PREFIX "/libexec/termux-x11/loader.apk"
#define TX11_SOCK_DIR TX11_PREFIX "/tmp/.X11-unix"
#define TX11_PACKAGES "/data/system/packages.list"

/* VirGL Paths (Android only) */
#define DS_VIRGL_SOCKET "/tmp/.virgl_test"
#define TX11_VIRGL_SOCKET TX11_PREFIX "/tmp/.virgl_test"
#define TX11_VIRGL_BIN TX11_PREFIX "/bin/virgl_test_server_android"

/* PulseAudio paths (Android only) */
#define DS_PULSE_SOCKET "/tmp/.pulse-socket"
#define TX11_PULSE_SOCKET TX11_PREFIX "/tmp/.pulse-socket"
#define TX11_PULSE_BIN TX11_PREFIX "/bin/pulseaudio"
#define TX11_PACTL_BIN TX11_PREFIX "/bin/pactl"
#define TX11_PULSE_DEFAULT_SINK "AAudio_sink"

#endif /* DS_ANDROID_TERMUX_H */
