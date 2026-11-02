/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_DAEMON_SERVER_H
#define DS_DAEMON_SERVER_H

int ds_daemon_run(int foreground, char **argv);
int ds_daemon_probe(void);

#endif /* DS_DAEMON_SERVER_H */
