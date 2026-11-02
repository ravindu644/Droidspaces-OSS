/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CGROUP_DISCOVER_H
#define DS_CGROUP_DISCOVER_H

int ds_cgroup_host_is_v2(void);
int ds_cgroup_has_controller(const char *name);
void print_cgroup_status(struct ds_config *cfg);
int ds_cgroup_has_swap_accounting(void);
int ds_cgroup_ctrl_dir(const char *ctrl, const char *container_name, char *dir,
                       size_t size);

#endif /* DS_CGROUP_DISCOVER_H */
