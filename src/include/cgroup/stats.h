/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CGROUP_STATS_H
#define DS_CGROUP_STATS_H

int ds_cgroup_count_tasks(const char *container_name, int *running, int *total);
int ds_cgroup_cpu_times(const char *container_name, long long *user_us,
                        long long *system_us);
int ds_cgroup_get_usage(const char *container_name, long long *mem,
                        long long *file_cache, long long *cpu_us,
                        long long *pids);

#endif /* DS_CGROUP_STATS_H */
