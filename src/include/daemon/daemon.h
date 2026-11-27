/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_DAEMON_DAEMON_H
#define DS_DAEMON_DAEMON_H

/* client.c */
/* cmd is the sub-command main() discovered, so a payload word like "run"
 * inside the command does not decide whether the session is interactive.
 * want_stdin is --stdin: relay our stdin to a pipe-mode run instead of
 * handing it /dev/null. */
int ds_client_run(int argc, char **argv, const char *cmd, int want_stdin);

/* server.c */
int ds_daemon_run(int foreground, char **argv);
int ds_daemon_probe(void);

#endif /* DS_DAEMON_DAEMON_H */
