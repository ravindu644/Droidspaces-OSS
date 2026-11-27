/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_ANDROID_H
#define DS_ANDROID_ANDROID_H

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

/* android.c */
void android_optimizations(int enable);
void android_remount_data_suid(void);
int android_setup_storage(const char *rootfs_path);

/* anland/anland.c */
int ds_anland_daemon_start(struct ds_config *cfg);
void ds_anland_daemon_stop(struct ds_config *cfg);
int ds_setup_anland_socket(struct ds_config *cfg);
void ds_anland_load_sock(struct ds_config *cfg);

/* firmware.c */
void firmware_path_add(const char *fw_path);
void firmware_path_remove(const char *fw_path);

/* helpers.c */
void ds_android_helpers_sync(struct ds_config *cfg);

/* pulseaudio.c */
void ds_pulse_daemon_stop(struct ds_config *cfg);
int ds_setup_pulse_socket(struct ds_config *cfg);

/* socket.c */
int ds_bind_mount_socket(const char *src, const char *dst, uid_t uid,
                         const char *label);

/* spawn.c */
/* Daemon lifecycle helpers */
typedef void (*ds_child_fn)(int ready_fd, void *user_data);
pid_t ds_daemon_read_pid(const char *filename);
void ds_daemon_write_pid(const char *filename, pid_t pid, const char *flags);
void ds_daemon_remove_pid(const char *filename);
void ds_global_daemon_stop(int (*check_fn)(void), pid_t cached_pid,
                           pid_t *pid_out, const char *pidfile,
                           const char *sock_path, const char *tag);

/* Common preamble for a forked long-lived daemon child (audio/GPU/X11 helper):
 * ignore terminal-disconnect signals so it outlives the launching session and
 * protect it from the OOM killer.  Must run while still root. */
void ds_daemon_child_preamble(void);
pid_t ds_spawn_daemon(ds_child_fn child_fn, void *user_data,
                      const char *log_file, const char *tag, const char *label);

/* virgl.c */
void ds_virgl_daemon_stop(struct ds_config *cfg);
int ds_setup_virgl_socket(struct ds_config *cfg);

/* x11.c */
void ds_x11_daemon_stop(struct ds_config *cfg);
int ds_setup_x11_socket(struct ds_config *cfg);

#endif /* DS_ANDROID_ANDROID_H */
