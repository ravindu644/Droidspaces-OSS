/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_EXEC_H
#define DS_CONTAINER_EXEC_H

int enter_namespace(pid_t pid, struct ds_config *cfg);
int enter_rootfs(struct ds_config *cfg, const char *user);
int run_in_rootfs(struct ds_config *cfg, char **argv, const char *as_user);

#endif /* DS_CONTAINER_EXEC_H */
