/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ENV_ENVIRONMENT_H
#define DS_ENV_ENVIRONMENT_H

void free_config_env_vars(struct ds_config *cfg);
void load_etc_environment(void);
void ds_env_boot_setup(struct ds_config *cfg);
void ds_env_save(const char *path, struct ds_config *cfg);
void parse_env_file_to_config(struct ds_config *cfg);

#endif /* DS_ENV_ENVIRONMENT_H */
