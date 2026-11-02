/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_PATHS_H
#define DS_CORE_PATHS_H

char *ds_resolve_path_arg(const char *path);
int is_subpath(const char *parent, const char *child);

#endif /* DS_CORE_PATHS_H */
