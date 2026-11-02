/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONFIG_SAVE_H
#define DS_CONFIG_SAVE_H

char *ds_config_serialize(struct ds_config *cfg, size_t *len, size_t *known);
int ds_config_save(const char *config_path, struct ds_config *cfg);
int ds_config_save_by_name(const char *name, struct ds_config *cfg);

#endif /* DS_CONFIG_SAVE_H */
