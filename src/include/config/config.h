/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONFIG_CONFIG_H
#define DS_CONFIG_CONFIG_H

void sanitize_container_name(const char *name, char *out, size_t size);
int validate_container_name(const char *name);
int reject_container_name(const char *name);

void ds_config_init(struct ds_config *cfg);
void ds_config_free(struct ds_config *cfg);
void ds_config_reset_defaults(struct ds_config *cfg);
void apply_reset_config(struct ds_config *cfg);

#endif /* DS_CONFIG_CONFIG_H */
