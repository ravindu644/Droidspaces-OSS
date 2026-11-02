/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NETNS_H
#define DS_NET_NETNS_H

void ds_net_prepare_host_veth(const char *ifname);
void ds_net_steer_rx(const char *sys, const char *ifname);

#endif /* DS_NET_NETNS_H */
