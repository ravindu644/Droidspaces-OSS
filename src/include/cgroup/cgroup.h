/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CGROUP_CGROUP_H
#define DS_CGROUP_CGROUP_H

/* discover.c */
int ds_cgroup_host_is_v2(void);
int ds_cgroup_has_controller(const char *name);
void print_cgroup_status(struct ds_config *cfg);
int ds_cgroup_has_swap_accounting(void);
int ds_cgroup_ctrl_dir(const char *ctrl, const char *container_name, char *dir,
                       size_t size);

/* lifecycle.c */
int ds_cgroup_attach(const char *container_name);

/* Remove the entire /sys/fs/cgroup/droidspaces/<name>/ subtree on stop. */
void ds_cgroup_cleanup_container(const char *container_name);
void ds_cgroup_setup(struct ds_config *cfg);
void ds_cgroup_join(const char *container_name);

/* limits.c */
void ds_cgroup_apply_limits(struct ds_config *cfg);
void ds_cgroup_get_limits(const char *container_name, long long *mem,
                          long long *cpu_quota, long long *cpu_period,
                          long long *pids, int *swap_capped);

/* mount.c */
int setup_cgroups(int is_systemd, int force_cgroupv1);
void ds_cgroup_host_bootstrap(int force_cgroupv1);

/* stats.c */
int ds_cgroup_count_tasks(const char *container_name, int *running, int *total);
int ds_cgroup_cpu_times(const char *container_name, long long *user_us,
                        long long *system_us);
int ds_cgroup_get_usage(const char *container_name, long long *mem,
                        long long *file_cache, long long *cpu_us,
                        long long *pids);

#endif /* DS_CGROUP_CGROUP_H */
