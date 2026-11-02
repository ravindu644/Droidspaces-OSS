/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CLI_CHECK_H
#define DS_CLI_CHECK_H

int check_ns(int flag, const char *name);
int check_requirements_hw(int hw_access);
int check_requirements_detailed(int json_output);

#endif /* DS_CLI_CHECK_H */
