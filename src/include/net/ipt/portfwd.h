/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_IPT_PORTFWD_H
#define DS_NET_IPT_PORTFWD_H

int ds_ipt_add_portforwards(struct ds_port_forward *pfs, int count,
                            const char *container_ip);
int ds_ipt_remove_portforwards(struct ds_config *cfg);

#endif /* DS_NET_IPT_PORTFWD_H */
