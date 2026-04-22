/*
 * Droidspaces v5 - Resource Virtualization Layer
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_VIRTUALIZE_H
#define DS_VIRTUALIZE_H

#include "droidspace.h"

/* Generate virtualized /proc/meminfo content */
int ds_virtualize_meminfo(struct ds_config *cfg, char **buf_out, size_t *size_out);

/* Generate virtualized /proc/cpuinfo content */
int ds_virtualize_cpuinfo(struct ds_config *cfg, char **buf_out, size_t *size_out);

/* Generate virtualized /proc/stat content */
int ds_virtualize_stat(struct ds_config *cfg, char **buf_out, size_t *size_out);

/* Generate virtualized /proc/uptime content */
int ds_virtualize_uptime(struct ds_config *cfg, char **buf_out, size_t *size_out);

/* Generate virtualized /proc/loadavg content */
int ds_virtualize_loadavg(struct ds_config *cfg, char **buf_out, size_t *size_out);

/* Generate virtualized CPU range for sysfs (e.g. 0-3) */
int ds_virtualize_cpu_sysfs(struct ds_config *cfg, char **buf_out, size_t *size_out);

/* Initialize virtual proc directory in container rootfs (pre-pivot) */
int ds_virtualize_init(struct ds_config *cfg);

/* Update dynamic virtual files from monitor process */
void ds_virtualize_update(struct ds_config *cfg);

/* Get PID namespace inode for a given PID */
unsigned long ds_get_pid_ns_inode(pid_t pid);

#endif /* DS_VIRTUALIZE_H */
