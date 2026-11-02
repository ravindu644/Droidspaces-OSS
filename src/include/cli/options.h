/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CLI_OPTIONS_H
#define DS_CLI_OPTIONS_H

void ds_resolve_argv_paths(int argc, char **argv);
int parse_and_validate_names(const char *optarg, char *out_buf,
                             size_t out_size);

#endif /* DS_CLI_OPTIONS_H */
