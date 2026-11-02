/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_SOCKET_H
#define DS_ANDROID_SOCKET_H

int ds_bind_mount_socket(const char *src, const char *dst, uid_t uid,
                         const char *label);
/* Bridge a Termux host socket (DS_TERMUX_TMP_OLDROOT/<leaf>) into the container
 * at dst (owned by the host socket's uid), then export env_key=env_val.
 * Best-effort: warns and skips if the source is absent.  Always returns 0. */
int ds_bridge_termux_socket(const char *leaf, const char *dst,
                            const char *env_key, const char *env_val,
                            const char *label);

#endif /* DS_ANDROID_SOCKET_H */
