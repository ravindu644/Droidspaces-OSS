/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_HELPERS_H
#define DS_ANDROID_HELPERS_H

void ds_android_helpers_sync(struct ds_config *cfg);
int wait_for_socket_or_death(pid_t pid, const char *path, int timeout_ms,
                             int interval_us);

#endif /* DS_ANDROID_HELPERS_H */
