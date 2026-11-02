/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONFIG_LOAD_H
#define DS_CONFIG_LOAD_H

int ds_config_load(const char *config_path, struct ds_config *cfg);
int ds_config_load_by_name(const char *name, struct ds_config *cfg);
int ds_config_peek(const char *name, struct ds_config *cfg);
int ds_config_peek_path(const char *config_path, struct ds_config *cfg);
void free_config_unknown_lines(struct ds_config *cfg);
char *ds_config_auto_path(const char *rootfs_path);

#endif /* DS_CONFIG_LOAD_H */
