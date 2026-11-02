/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CGROUP_INTERNAL_H
#define DS_CGROUP_INTERNAL_H

enum { V1_CREATE, V1_JOIN, V1_REMOVE };

int ctrl_in_list(const char *list, const char *name);
void rmdir_cgroup_tree(const char *path);
void v1_own_mount_dir(char *buf, size_t size);
int is_container_mount(const char *mp);
int find_host_cgroup_mount(const char *ctrl, char *buf, size_t size);
void each_v1_controller(void (*fn)(const char *name, void *arg), void *arg);
long long parse_cgroup_ll(const char *buf);
void mkdir_fresh(const char *dir);
void v1_each(const char *container_name, int op);
void v1_setup(struct ds_config *cfg);

#endif /* DS_CGROUP_INTERNAL_H */
