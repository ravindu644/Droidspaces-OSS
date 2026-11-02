/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_CLEANUP_H
#define DS_CONTAINER_CLEANUP_H

void cleanup_container_resources(struct ds_config *cfg, pid_t pid,
                                 int skip_unmount, int force_cleanup);

#endif /* DS_CONTAINER_CLEANUP_H */
