/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NAT_VETH_H
#define DS_NET_NAT_VETH_H

/* NAT veth/bridge lifecycle */
int setup_veth_host_side(struct ds_config *cfg, pid_t child_pid);

#endif /* DS_NET_NAT_VETH_H */
