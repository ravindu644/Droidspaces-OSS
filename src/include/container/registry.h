/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_REGISTRY_H
#define DS_CONTAINER_REGISTRY_H

int read_and_validate_pid(const char *pidfile, pid_t *pid_out);

int resolve_pidfile_from_name(const char *name, char *pidfile, size_t size);
int is_container_running(struct ds_config *cfg, pid_t *pid_out);
int is_container_init(pid_t pid);
int count_running_containers(char *first_name, size_t size);
pid_t find_container_init_pid(const char *uuid);
pid_t find_container_by_name(const char *name);
int check_selinux_permissive_needs(void);
int check_x11_needs(void);
int check_virgl_needs(void);
int check_pulse_needs(void);

/*
 * ds_feature_needs - generic feature-needs scanner.
 * Pass offsetof(struct ds_config, <field>) for the boolean flag to check.
 * Returns: 1=running, 0=installed-but-idle, -1=not-installed
 */
int ds_feature_needs(size_t cfg_flag_offset);

int next_running(DIR *d, struct ds_config *cfg, pid_t *pid);

int is_valid_container_pid(pid_t pid);

#endif /* DS_CONTAINER_REGISTRY_H */
