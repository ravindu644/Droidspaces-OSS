/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_INTERNAL_H
#define DS_CONTAINER_INTERNAL_H

int is_pid_file(const char *name);
void get_container_name_from_pidfile(const char *pidfile, char *name,
                                     size_t size);
void prune_stale_pidfile(const char *name, const char *pidfile);

int start_rootfs_locked(struct ds_config *cfg, int *lock_fd, int reuse_mount);
int lifecycle_lock_or_complain(struct ds_config *cfg, const char *verb);
int stop_rootfs_locked(struct ds_config *caller_cfg, int skip_unmount,
                       int timeout_seconds);

#endif /* DS_CONTAINER_INTERNAL_H */
