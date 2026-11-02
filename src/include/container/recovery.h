/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_RECOVERY_H
#define DS_CONTAINER_RECOVERY_H

int ds_metadata_sync(pid_t pid);
int scan_containers(void);

#endif /* DS_CONTAINER_RECOVERY_H */
