/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_PROC_H
#define DS_CORE_PROC_H

long ds_clk_tck(void);
long ds_get_container_uptime(pid_t pid);

/* Field 22 of /proc/<pid>/stat in clock ticks since boot, 0 if unreadable. */
unsigned long long proc_starttime(pid_t pid);
const char *proc_stat_field(pid_t pid, int n, char *buf, size_t size);
pid_t proc_ppid(pid_t pid);
uid_t proc_uid(pid_t pid);
int get_kernel_version(int *major, int *minor);
int collect_pids(pid_t **pids_out, size_t *count_out);
int build_proc_root_path(pid_t pid, const char *suffix, char *buf, size_t size);
int ds_open_in_root(pid_t pid, const char *path, int flags);
void check_kernel_recommendation(void);

#endif /* DS_CORE_PROC_H */
