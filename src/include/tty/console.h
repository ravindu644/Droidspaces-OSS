/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_TTY_CONSOLE_H
#define DS_TTY_CONSOLE_H

int ds_console_send(int sock, int master, int slave);
int ds_console_recv(int sock, int *master, int *slave);
int console_monitor_loop(int console_sock, pid_t monitor_pid,
                         struct ds_config *cfg);

#endif /* DS_TTY_CONSOLE_H */
