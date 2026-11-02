/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_INSPECT_H
#define DS_CONTAINER_INSPECT_H

int is_systemd_rootfs(const char *path);

ds_init_type_t detect_container_init(const char *path);
int get_user_shell(const char *user, char *shell_buf, size_t size);

#endif /* DS_CONTAINER_INSPECT_H */
