/*
 * Droidspaces v6 - Resource Visibility Virtualization
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Zero-dependency LXCFS alternative. Virtualizes /proc/meminfo,
 * /proc/cpuinfo, /proc/stat, /proc/uptime, /proc/loadavg, and
 * /sys/devices/system/cpu/{online,possible,present} based on active
 * cgroup v2 resource limits. Only active limiters incur overhead.
 */

#ifndef DS_VIRTUALIZE_H
#define DS_VIRTUALIZE_H

#include "droidspace.h"

/* Initialize virtual proc in container rootfs (called inside container,
 * pre-exec). Creates tmpfs at /run/droidspaces/vproc, writes and bind-mounts
 * only the proc/sysfs files relevant to active limits. No-op if no limits set.
 */
int ds_virtualize_init(struct ds_config *cfg);

/* Update dynamic virtual files from monitor process every 500ms.
 * Writes in-place to preserve bind-mount inodes. Guards against PID recycling
 * via ns_inode check. No-op if no limits set or container_pid is 0. */
void ds_virtualize_update(struct ds_config *cfg);

/* Return PID namespace inode for identity verification. Returns 0 on error. */
unsigned long ds_get_pid_ns_inode(pid_t pid);

#endif /* DS_VIRTUALIZE_H */
