/*
 * Anland 5.x display broker.
 *
 * Vendored from https://github.com/superturtlee/anland, branch legacy,
 * libdisplay_daemon/, and adapted for Droidspaces: see display_daemon.c.
 *
 * The broker stashes the consumer's screen info and deposited fds and hands
 * them to the producer. All state lives in an opaque daemon_ctx. The library
 * installs no signal handlers; the hosting process (anland.c) owns shutdown
 * and calls daemon_stop from its SIGTERM handler.
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

/* Run the epoll loop until daemon_stop() is called. Blocking. */
void daemon_run(daemon_ctx *ctx);

/* Ask the loop to exit. Only sets a flag, so it is safe from a signal handler.
 */
void daemon_stop(daemon_ctx *ctx);

/* Close every client and socket, unlink the socket path, free ctx. */
void daemon_destroy(daemon_ctx *ctx);

#endif /* DISPLAY_DAEMON_H */
