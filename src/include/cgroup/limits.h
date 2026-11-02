/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CGROUP_LIMITS_H
#define DS_CGROUP_LIMITS_H

void ds_cgroup_apply_limits(struct ds_config *cfg);
void ds_cgroup_get_limits(const char *container_name, long long *mem,
                          long long *cpu_quota, long long *cpu_period,
                          long long *pids, int *swap_capped);

#endif /* DS_CGROUP_LIMITS_H */
