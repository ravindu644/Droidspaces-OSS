/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_STOP_H
#define DS_CONTAINER_STOP_H

int stop_rootfs(struct ds_config *cfg, int skip_unmount);
int stop_rootfs_with_timeout(struct ds_config *cfg, int skip_unmount,
                             int timeout_seconds);

#endif /* DS_CONTAINER_STOP_H */
