/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_VIRT_VPROC_H
#define DS_VIRT_VPROC_H

/* virtualize.c: the LXCFS-style views of /proc and /sys/devices/system/cpu */

/* Inside the container before init execs: bind-mounts the virtual files the
 * active limits need, no-op without limits. */
int ds_virtualize_init(struct ds_config *cfg);

/* From the monitor every 500 ms: rewrites the dynamic files in place so the
 * bind-mounted inodes survive. */
void ds_virtualize_update(struct ds_config *cfg);
unsigned long ds_get_pid_ns_inode(pid_t pid);

#endif /* DS_VIRT_VPROC_H */
