/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CGROUP_LIFECYCLE_H
#define DS_CGROUP_LIFECYCLE_H

int ds_cgroup_attach(const char *container_name);

/* Remove the entire /sys/fs/cgroup/droidspaces/<name>/ subtree on stop. */
void ds_cgroup_cleanup_container(const char *container_name);
void ds_cgroup_setup(struct ds_config *cfg);
void ds_cgroup_join(const char *container_name);

#endif /* DS_CGROUP_LIFECYCLE_H */
