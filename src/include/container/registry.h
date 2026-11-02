/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_REGISTRY_H
#define DS_CONTAINER_REGISTRY_H

int read_and_validate_pid(const char *pidfile, pid_t *pid_out);

#endif /* DS_CONTAINER_REGISTRY_H */
