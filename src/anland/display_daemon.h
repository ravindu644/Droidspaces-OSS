#ifndef DISPLAY_DAEMON_H
#define DISPLAY_DAEMON_H

/*
 * Relay daemon that brokers the consumer/producer handshake: it stashes the
 * consumer's screen info and deposited fds, then hands them to the producer.
 *
 * All runtime state lives in an opaque daemon_ctx, so multiple independent
 * instances can coexist in one process. The library installs no signal handlers
 * and touches no process-global state; the hosting program owns that (see
 * daemon/daemon.c for the standalone ELF).
 */
typedef struct daemon_ctx daemon_ctx;

/* Create an instance listening on sock_path (internally unlink+bind+listen and
 * sets up epoll). Returns 0 on success, -1 on failure. */
int daemon_create(daemon_ctx **out, const char *sock_path);

/* Run the epoll loop until daemon_stop() is called. Blocking. */
void daemon_run(daemon_ctx *ctx);

/* Request the run loop to exit. Only sets a volatile flag, so it is safe to
 * call from a signal handler or another thread. */
void daemon_stop(daemon_ctx *ctx);

/* Close all clients and sockets, unlink the socket path, and free ctx. */
void daemon_destroy(daemon_ctx *ctx);

#endif
