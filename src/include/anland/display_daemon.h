/*
 * Anland 5.x display broker.
 *
 * Vendored from https://github.com/superturtlee/anland, branch legacy,
 * libdisplay_daemon/, and adapted for Droidspaces: see display_daemon.c.
 *
 * The broker stashes the consumer's screen info and deposited fds and hands
 * them to the producer. All state lives in an opaque daemon_ctx. The library
 * installs no signal handlers and never returns from daemon_run: the hosting
 * process (anland.c) ends with the SIGTERM ds_global_daemon_stop sends.
 *
 * Copyright (C) 2026 the Anland authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DISPLAY_DAEMON_H
#define DISPLAY_DAEMON_H

typedef struct daemon_ctx daemon_ctx;

/* Bind and listen on sock_path and set up epoll. 0 on success, -1 on failure.
 */
int daemon_create(daemon_ctx **out, const char *sock_path);

/* Run the epoll loop. Blocking, until the process is killed. */
void daemon_run(daemon_ctx *ctx);

/* Close every client and socket, unlink the socket path, free ctx. */
void daemon_destroy(daemon_ctx *ctx);

#endif /* DISPLAY_DAEMON_H */
