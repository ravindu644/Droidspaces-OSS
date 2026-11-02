/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_MACVLAN_H
#define DS_NET_MACVLAN_H

/* Macvlan: create eth0 in the container on its parent NIC. Called at start
 * and then on every heartbeat, since unplugging the parent deletes it. */
int ds_net_macvlan_wire(struct ds_config *cfg, pid_t init_pid, int at_start);

/* Refuse a macvlan start that could never get a network. 0 or -1. */
int ds_net_macvlan_check(const struct ds_config *cfg);

#endif /* DS_NET_MACVLAN_H */
