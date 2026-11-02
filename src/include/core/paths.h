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

const char *get_workspace_dir(void);
const char *get_pids_dir(void);
const char *get_net_dir(void);
const char *get_logs_dir(void);
int ensure_workspace(void);

#endif /* DS_CORE_PATHS_H */
