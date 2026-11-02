/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_SECURITY_PEER_AUTH_H
#define DS_SECURITY_PEER_AUTH_H

/* Return 1 if peer_pid shares this process's PID namespace (or if it cannot be
 * determined), 0 only on a positive mismatch.  Used by the abstract-socket
 * authorizers to reject host-net container peers. */
int ds_peer_in_pidns(pid_t peer_pid);
/* Authorize a connected AF_UNIX peer: allow root, or a member of group_name,
 * but only when the peer shares our PID namespace.  Returns 1 if authorized. */
int ds_peer_authorized(int fd, const char *group_name);

#endif /* DS_SECURITY_PEER_AUTH_H */
