/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_SPAWN_H
#define DS_ANDROID_SPAWN_H

/* Daemon lifecycle helpers */
typedef void (*ds_child_fn)(int ready_fd, void *user_data);
pid_t ds_daemon_read_pid(const char *filename);
void ds_daemon_write_pid(const char *filename, pid_t pid, const char *flags);
void ds_daemon_remove_pid(const char *filename);
pid_t ds_resolve_daemon_pid(pid_t cached, const char *pidfile);
void ds_global_daemon_stop(int (*check_fn)(void), pid_t cached_pid,
                           pid_t *pid_out, const char *pidfile,
                           const char *sock_path, const char *tag);
pid_t ds_global_daemon_reuse(int (*check_fn)(void), const char *pidfile,
                             const char *flags, const char *sock_path,
                             const char *tag);

/* Common preamble for a forked long-lived daemon child (audio/GPU/X11 helper):
 * ignore terminal-disconnect signals so it outlives the launching session and
 * protect it from the OOM killer.  Must run while still root. */
void ds_daemon_child_preamble(void);
void ds_spawn_log_relay(int pipe_read_fd, const char *log_file,
                        const char *tag);
pid_t ds_spawn_daemon(ds_child_fn child_fn, void *user_data,
                      const char *log_file, const char *tag, const char *label);

#endif /* DS_ANDROID_SPAWN_H */
