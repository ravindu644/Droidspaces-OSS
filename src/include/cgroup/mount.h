/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CGROUP_MOUNT_H
#define DS_CGROUP_MOUNT_H

int setup_cgroups(int is_systemd, int force_cgroupv1);
void ds_cgroup_host_bootstrap(int force_cgroupv1);

#endif /* DS_CGROUP_MOUNT_H */
