/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_MONITOR_INTERNAL_H
#define DS_MONITOR_INTERNAL_H

void ds_console_drain(int master_fd, int log_fd, size_t *logged);
int console_log_open(const struct ds_config *cfg);
void console_adopt(struct ds_config *cfg, int *log_fd);

#endif /* DS_MONITOR_INTERNAL_H */
