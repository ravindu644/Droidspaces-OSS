/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONFIG_PARSE_H
#define DS_CONFIG_PARSE_H

void sort_bind_mounts(struct ds_config *cfg);
int validate_bind_destination(const char *dest);

#endif /* DS_CONFIG_PARSE_H */
