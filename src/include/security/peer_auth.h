/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_SECURITY_PEER_AUTH_H
#define DS_SECURITY_PEER_AUTH_H

/* Return 1 only when peer_pid is confirmed to share this process's PID
 * namespace, 0 otherwise: a dead, recycled or untranslatable pid is denied.
 * This is a trust boundary and fails closed; the one positive exception is a
 * kernel without CONFIG_PID_NS, which has a single namespace. Used by the
 * abstract-socket authorizers to reject host-net container peers. */
int ds_peer_in_pidns(pid_t peer_pid);
/* Authorize a connected AF_UNIX peer: allow root, or a member of group_name,
 * but only when the peer shares our PID namespace.  Returns 1 if authorized. */
int ds_peer_authorized(int fd, const char *group_name);

#endif /* DS_SECURITY_PEER_AUTH_H */
