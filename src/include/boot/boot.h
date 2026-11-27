/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_BOOT_BOOT_H
#define DS_BOOT_BOOT_H

/* init.c */
int internal_boot(struct ds_config *cfg);

/* env.c */
void load_etc_environment(void);
void ds_env_boot_setup(struct ds_config *cfg);
void ds_env_save(const char *path, struct ds_config *cfg);

#endif /* DS_BOOT_BOOT_H */
