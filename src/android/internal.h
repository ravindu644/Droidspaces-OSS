/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_INTERNAL_H
#define DS_ANDROID_INTERNAL_H

/* android.c */
/* SELinux + Termux privilege helpers */
const char *ds_extract_mls(const char *ctx);
void ds_selinux_dyntransition(const char *mls);
int ds_drop_privileges(int uid);
int ds_resolve_termux_uid(void);

/* helpers.c */
int wait_for_socket_or_death(pid_t pid, const char *path, int timeout_ms,
                             int interval_us);

/* pulseaudio.c */
int ds_pulse_daemon_start(struct ds_config *cfg);

/* socket.c */
/* Bridge a Termux host socket (DS_TERMUX_TMP_OLDROOT/<leaf>) into the container
 * at dst (owned by the host socket's uid), then export env_key=env_val.
 * Best-effort: warns and skips if the source is absent.  Always returns 0. */
int ds_bridge_termux_socket(const char *leaf, const char *dst,
                            const char *env_key, const char *env_val,
                            const char *label);

/* spawn.c */
pid_t ds_global_daemon_reuse(int (*check_fn)(void), const char *pidfile,
                             const char *flags, const char *sock_path,
                             const char *tag);

/* virgl.c */
int ds_virgl_daemon_start(struct ds_config *cfg);

/* x11.c */
int ds_x11_daemon_start(struct ds_config *cfg);

#endif /* DS_ANDROID_INTERNAL_H */
