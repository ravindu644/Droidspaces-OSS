/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONFIG_LOAD_H
#define DS_CONFIG_LOAD_H

int ds_config_load(const char *config_path, struct ds_config *cfg);
int ds_config_load_file(FILE *f, struct ds_config *cfg);
/* <workspace>/Containers/<sanitized name>/container.config. -1 for a name
 * validate_container_name refuses. */
int ds_config_host_path(const char *name, char *buf, size_t size);
/* The host config of name, by path, never the booted one: for the reloads
 * that want the user's latest edits. */
int ds_config_load_host(const char *name, struct ds_config *cfg);
int ds_config_load_by_name(const char *name, struct ds_config *cfg);
int ds_config_peek(const char *name, struct ds_config *cfg);
int ds_config_peek_path(const char *config_path, struct ds_config *cfg);
void free_config_unknown_lines(struct ds_config *cfg);
char *ds_config_auto_path(const char *rootfs_path);

#endif /* DS_CONFIG_LOAD_H */
